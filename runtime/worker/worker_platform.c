/*
 * xbintsc runtime — worker transport, operating-system half.
 *
 * Everything that differs between POSIX and Windows lives here:
 *
 *   POSIX                          Windows
 *   -----------------------------  -----------------------------------------
 *   socket()/close()               SOCKET/closesocket() (Winsock is started
 *                                  by `xt_set_program_args` at startup)
 *   fork() + execvp()              _spawnvp(_P_NOWAIT) -> process HANDLE
 *   close(3..255) in the child     only inheritable handles cross the
 *                                  boundary, so the control socket is marked
 *                                  non-inheritable before the spawn
 *   waitpid(pid, WNOHANG)          WaitForSingleObject(handle, 0)
 *   kill(pid, SIGKILL)             TerminateProcess(handle, 1)
 *   setenv/unsetenv                _putenv_s
 *
 * The control channel is a loopback TCP socket on both platforms: Windows
 * `select(2)` only accepts sockets, and one transport keeps the protocol
 * single. The worker's stdout/stderr stay inherited, so `console.log` inside a
 * worker still reaches the terminal and never corrupts the protocol.
 */

#include "worker_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <process.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef int xt_socklen_t;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
typedef socklen_t xt_socklen_t;
#endif

char *xt_wrk_duplicate(const char *text) {
  size_t length = text ? strlen(text) : 0;
  char *copy = (char *)malloc(length + 1);
  if (!copy) return NULL;
  if (length > 0) memcpy(copy, text, length);
  copy[length] = 0;
  return copy;
}

/* -- environment ---------------------------------------------------------- */

int xt_wrk_env_set(const char *key, const char *value) {
  if (!value) value = "";
#if defined(_WIN32)
  return _putenv_s(key, value) == 0;
#else
  return setenv(key, value, 1) == 0;
#endif
}

void xt_wrk_env_unset(const char *key) {
#if defined(_WIN32)
  _putenv_s(key, "");
#else
  unsetenv(key);
#endif
}

const char *xt_wrk_env_get(const char *key) { return getenv(key); }

static int xt_wrk_os_pid(void) {
#if defined(_WIN32)
  return _getpid();
#else
  return (int)getpid();
#endif
}

/* -- sockets -------------------------------------------------------------- */

int xt_wrk_would_block(void) {
#if defined(_WIN32)
  return WSAGetLastError() == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

void xt_wrk_close_fd(int fd) {
  if (fd < 0) return;
#if defined(_WIN32)
  closesocket((SOCKET)(uintptr_t)fd);
#else
  close(fd);
#endif
}

/* Keep the control socket out of the child when the host inherits handles. On
 * POSIX the child closes 3..255 itself after fork instead. */
static void xt_wrk_no_inherit(int fd) {
#if defined(_WIN32)
  SetHandleInformation((HANDLE)(uintptr_t)fd, HANDLE_FLAG_INHERIT, 0);
#else
  (void)fd;
#endif
}

int xt_wrk_set_nonblocking(int fd) {
#if defined(_WIN32)
  u_long mode = 1;
  return ioctlsocket((SOCKET)(uintptr_t)fd, FIONBIO, &mode) == 0;
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return 0;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

/* A peer may disappear between the check and the write; never let that kill the
 * process through SIGPIPE. Linux takes MSG_NOSIGNAL per call, macOS/BSD set the
 * socket option once. */
static void xt_wrk_no_sigpipe(int fd) {
#if defined(SO_NOSIGPIPE)
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&yes, sizeof(yes));
#else
  (void)fd;
#endif
}

int xt_wrk_listen_loopback(int *out_port) {
  *out_port = 0;
  int fd = (int)(intptr_t)socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
      listen(fd, 8) != 0) {
    xt_wrk_close_fd(fd);
    return -1;
  }
  xt_socklen_t length = (xt_socklen_t)sizeof(address);
  if (getsockname(fd, (struct sockaddr *)&address, &length) != 0) {
    xt_wrk_close_fd(fd);
    return -1;
  }
  *out_port = (int)ntohs(address.sin_port);
  xt_wrk_no_sigpipe(fd);
  xt_wrk_no_inherit(fd);
  /* `accept` is also called when a worker is drained after exit, when no
   * connection may be pending; a blocking listener would hang the program. */
  xt_wrk_set_nonblocking(fd);
  return fd;
}

int xt_wrk_accept(int listen_fd) {
  struct sockaddr_in address;
  xt_socklen_t length = (xt_socklen_t)sizeof(address);
  int fd = (int)(intptr_t)accept(listen_fd, (struct sockaddr *)&address, &length);
  if (fd < 0) return -1;
  xt_wrk_no_inherit(fd);
  xt_wrk_set_nonblocking(fd);
  return fd;
}

int xt_wrk_connect_loopback(int port) {
  int fd = (int)(intptr_t)socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  xt_wrk_no_inherit(fd);
  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons((unsigned short)port);
  if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
    xt_wrk_close_fd(fd);
    return -1;
  }
  xt_wrk_no_sigpipe(fd);
  xt_wrk_set_nonblocking(fd);
  return fd;
}

/* Send as much as the socket accepts. Returns >= 0 bytes written, -1 on error.
 * A short write is not an error; the caller keeps the remainder. */
long xt_wrk_send_some(int fd, const char *data, size_t length) {
  if (length == 0) return 0;
  if (length > 1u << 20) length = 1u << 20;
#if defined(_WIN32)
  int written = send((SOCKET)(uintptr_t)fd, data, (int)length, 0);
#elif defined(MSG_NOSIGNAL)
  ssize_t written = send(fd, data, length, MSG_NOSIGNAL);
#else
  ssize_t written = send(fd, data, length, 0);
#endif
  if (written < 0) return -1;
  return (long)written;
}

/* Receive one chunk. > 0 bytes, 0 on clean EOF, -1 on error, -2 would block. */
long xt_wrk_recv_some(int fd, char *data, size_t capacity) {
#if defined(_WIN32)
  int got = recv((SOCKET)(uintptr_t)fd, data, (int)capacity, 0);
#else
  ssize_t got = recv(fd, data, capacity, 0);
#endif
  if (got > 0) return (long)got;
  if (got == 0) return 0;
  return xt_wrk_would_block() ? -2 : -1;
}

/* Used only on shutdown, to give a queued reply a chance to leave the socket
 * before it is closed. */
int xt_wrk_wait_writable(int fd, int timeout_ms) {
  fd_set write_set;
  FD_ZERO(&write_set);
#if defined(_WIN32)
  FD_SET((SOCKET)(uintptr_t)fd, &write_set);
#else
  FD_SET(fd, &write_set);
#endif
  struct timeval timeout;
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;
  return select(fd + 1, NULL, &write_set, NULL, &timeout) > 0;
}

/* Wait for data (or EOF) when a link is drained outside the reactor, e.g. after
 * its process was reaped. 1 when readable. */
int xt_wrk_wait_readable(int fd, int timeout_ms) {
  fd_set read_set;
  FD_ZERO(&read_set);
#if defined(_WIN32)
  FD_SET((SOCKET)(uintptr_t)fd, &read_set);
#else
  FD_SET(fd, &read_set);
#endif
  struct timeval timeout;
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;
  return select(fd + 1, &read_set, NULL, NULL, &timeout) > 0;
}

/* -- processes ------------------------------------------------------------ */

xt_wrk_pid xt_wrk_spawn_process(const char *path, char **error) {
  *error = NULL;
#if defined(_WIN32)
  const char *argv[2] = {path, NULL};
  intptr_t handle = _spawnvp(_P_NOWAIT, path, argv);
  if (handle == -1) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "failed to start worker process: %s", strerror(errno));
    *error = xt_wrk_duplicate(buffer);
    return 0;
  }
  return handle;
#else
  pid_t pid = fork();
  if (pid < 0) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "failed to fork worker process: %s", strerror(errno));
    *error = xt_wrk_duplicate(buffer);
    return 0;
  }
  if (pid == 0) {
    /* Only stdio belongs to the worker. An inherited listener or client socket
     * from another worker would delay that worker's EOF, so drop 3..255. The
     * program path is absolute (`import.meta.filename`), so `execv` avoids the
     * PATH search's allocation after a fork in a multithreaded process. */
    for (int fd = 3; fd < 256; fd++) close(fd);
    char *const argv[2] = {(char *)path, NULL};
    execv(path, argv);
    _exit(127);
  }
  return (xt_wrk_pid)pid;
#endif
}

int xt_wrk_process_exited(xt_wrk_pid pid, int *exit_code) {
  *exit_code = -1;
  if (pid <= 0) return 1;
#if defined(_WIN32)
  if (WaitForSingleObject((HANDLE)(uintptr_t)pid, 0) == WAIT_TIMEOUT) return 0;
  DWORD code = 0;
  if (GetExitCodeProcess((HANDLE)(uintptr_t)pid, &code)) *exit_code = (int)code;
  CloseHandle((HANDLE)(uintptr_t)pid);
  return 1;
#else
  int status = 0;
  pid_t result = waitpid((pid_t)pid, &status, WNOHANG);
  if (result == 0) return 0;
  if (result < 0) {
    *exit_code = -1;
    return 1;
  }
  if (WIFEXITED(status)) *exit_code = WEXITSTATUS(status);
  else if (WIFSIGNALED(status)) *exit_code = -WTERMSIG(status);
  return 1;
#endif
}

void xt_wrk_kill_process(xt_wrk_pid pid) {
  if (pid <= 0) return;
#if defined(_WIN32)
  TerminateProcess((HANDLE)(uintptr_t)pid, 1);
#else
  kill((pid_t)pid, SIGKILL);
#endif
}

/* -- token ---------------------------------------------------------------- */

void xt_wrk_make_token(char *out, size_t capacity) {
  static unsigned long counter = 0;
  unsigned long long seed =
      (unsigned long long)time(NULL) ^ ((unsigned long long)xt_wrk_os_pid() << 16) ^
      (unsigned long long)(counter++ * 2654435761u);
  static const char digits[] = "0123456789abcdef";
  size_t written = 0;
  for (int i = 0; i < 8 && written + 2 < capacity; i++) {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    unsigned long value = (unsigned long)((seed >> 33) & 0xff);
    out[written++] = digits[value >> 4];
    out[written++] = digits[value & 0x0f];
  }
  out[written] = 0;
}
