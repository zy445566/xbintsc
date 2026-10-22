/*
 * Node.js `child_process` module for xbintsc.
 *
 * Implements the synchronous `spawnSync(command, args, options)` surface the
 * compiler uses to shell out to clang:
 *
 *   const result = spawnSync("clang", ["--version"], { cwd, encoding: "utf8" });
 *   result.status / result.stdout / result.stderr
 *
 * `options.cwd` is honoured; stdout/stderr are always captured as UTF-8
 * strings. On Windows the process is run with `_spawnvp` and redirected
 * through temporary files (which also avoids pipe-buffer deadlocks).
 */

#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static char *build_argv(const char *command, xt_value args, int *out_count) {
  int32_t count = XT_IS_ARRAY(args) ? xt_array_size(args) : 0;
  xt_value *items = XT_IS_ARRAY(args) ? xt_array_items(args) : NULL;
  char **argv = (char **)malloc(sizeof(char *) * (size_t)(count + 2));
  if (!argv) {
    *out_count = 0;
    return NULL;
  }
  argv[0] = (char *)command;
  for (int32_t i = 0; i < count; i++) argv[i + 1] = (char *)xt_string_data(xt_to_string(items[i]));
  argv[count + 1] = NULL;
  *out_count = count + 1;
  return (char *)argv;
}

static const char *option_cwd(xt_value options) {
  if (!XT_IS_OBJECT(options)) return NULL;
  xt_value cwd = xt_object_get(options, xt_string_from_cstr("cwd"));
  if (!cwd || !XT_IS_STRING(cwd)) return NULL;
  return xt_string_data(cwd);
}

/* `stdio: "inherit"` hands the child the parent's stdout/stderr instead of
 * capturing them. Used by `xbintsc run` so a program's output streams live. */
static int option_stdio_inherit(xt_value options) {
  if (!XT_IS_OBJECT(options)) return 0;
  xt_value stdio = xt_object_get(options, xt_string_from_cstr("stdio"));
  return stdio && XT_IS_STRING(stdio) && strcmp(xt_string_data(stdio), "inherit") == 0;
}

static xt_value make_result(double status, const char *out, const char *err) {
  xt_value result = xt_object_new();
  xt_set(result, xt_string_from_cstr("status"), xt_number(status));
  xt_set(result, xt_string_from_cstr("stdout"), xt_string_from_cstr(out ? out : ""));
  xt_set(result, xt_string_from_cstr("stderr"), xt_string_from_cstr(err ? err : ""));
  return result;
}

#if defined(_WIN32)

/*
 * `spawnSync` captures stdout/stderr through anonymous pipes. Temporary files
 * are deliberately not used: `tmpfile()` creates its file in the root of the
 * current drive on the UCRT, which fails with "Permission denied" on machines
 * where that directory is not writable, and a restricted temp directory makes
 * every `spawnSync` fail as well. Pipes also avoid the pipe-buffer deadlock the
 * old temp-file design was working around, because one reader thread drains
 * each stream while the child runs.
 */
typedef struct {
  HANDLE read;
  char *data;
  size_t length;
  size_t capacity;
} CapturePipe;

static void capture_append(CapturePipe *pipe, const char *chunk, size_t count) {
  if (pipe->length + count + 1 > pipe->capacity) {
    size_t next = pipe->capacity ? pipe->capacity * 2 : 4096;
    while (next < pipe->length + count + 1) next *= 2;
    char *grown = (char *)realloc(pipe->data, next);
    if (!grown) return;
    pipe->data = grown;
    pipe->capacity = next;
  }
  memcpy(pipe->data + pipe->length, chunk, count);
  pipe->length += count;
  pipe->data[pipe->length] = 0;
}

static DWORD WINAPI capture_thread(LPVOID parameter) {
  CapturePipe *pipe = (CapturePipe *)parameter;
  char chunk[4096];
  DWORD got = 0;
  while (ReadFile(pipe->read, chunk, sizeof(chunk), &got, NULL) && got > 0) {
    capture_append(pipe, chunk, (size_t)got);
  }
  return 0;
}

static int create_capture_pipe(CapturePipe *pipe, HANDLE *write_end) {
  SECURITY_ATTRIBUTES attributes;
  HANDLE read_handle = NULL;
  HANDLE write_handle = NULL;
  attributes.nLength = sizeof(attributes);
  attributes.lpSecurityDescriptor = NULL;
  attributes.bInheritHandle = TRUE;
  if (!CreatePipe(&read_handle, &write_handle, &attributes, 0)) return 0;
  /* The child inherits the write end only. */
  SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0);
  pipe->read = read_handle;
  pipe->data = NULL;
  pipe->length = 0;
  pipe->capacity = 0;
  *write_end = write_handle;
  return 1;
}

static void close_capture_pipe(CapturePipe *pipe) {
  if (pipe->read != NULL) CloseHandle(pipe->read);
  free(pipe->data);
  pipe->read = NULL;
  pipe->data = NULL;
  pipe->length = 0;
  pipe->capacity = 0;
}

/* Quote one argument following the CommandLineToArgvW/MSVC rules. */
static char *append_quoted_argument(char *out, const char *argument) {
  if (*argument != 0 && strpbrk(argument, " \t\n\v\"") == NULL) {
    size_t length = strlen(argument);
    memcpy(out, argument, length);
    return out + length;
  }
  *out++ = '"';
  const char *cursor = argument;
  for (;;) {
    size_t backslashes = 0;
    while (*cursor == '\\') {
      cursor++;
      backslashes++;
    }
    if (*cursor == 0) {
      for (size_t i = 0; i < backslashes * 2; i++) *out++ = '\\';
      *out++ = '"';
      return out;
    }
    if (*cursor == '"') {
      for (size_t i = 0; i < backslashes * 2 + 1; i++) *out++ = '\\';
    } else {
      for (size_t i = 0; i < backslashes; i++) *out++ = '\\';
    }
    *out++ = *cursor++;
  }
}

/* Build the child command line; every argv element is quoted as needed. */
static char *build_command_line(char **child_argv, int count) {
  size_t capacity = 1;
  for (int i = 0; i < count; i++) capacity += strlen(child_argv[i]) * 2 + 4;
  char *line = (char *)malloc(capacity);
  if (!line) return NULL;
  char *cursor = line;
  for (int i = 0; i < count; i++) {
    if (i > 0) *cursor++ = ' ';
    cursor = append_quoted_argument(cursor, child_argv[i]);
  }
  *cursor = 0;
  return line;
}

xt_value xt_child_process_spawn_sync(int32_t argc, xt_value *argv) {
  const char *command = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : NULL;
  if (!command) command = "";
  xt_value args = argc > 1 ? argv[1] : XT_UNDEFINED;
  const char *cwd = argc > 2 ? option_cwd(argv[2]) : NULL;

  int count = 0;
  char **child_argv = (char **)build_argv(command, args, &count);
  if (!child_argv) return make_result(-1, "", "out of memory");
  char *command_line = build_command_line(child_argv, count);
  if (!command_line) {
    free(child_argv);
    return make_result(-1, "", "out of memory");
  }

  STARTUPINFOA startup;
  PROCESS_INFORMATION process;
  ZeroMemory(&startup, sizeof(startup));
  ZeroMemory(&process, sizeof(process));
  startup.cb = sizeof(startup);

  if (option_stdio_inherit(argc > 2 ? argv[2] : XT_UNDEFINED)) {
    BOOL created = CreateProcessA(
      NULL, command_line, NULL, NULL, TRUE, 0, NULL, cwd, &startup, &process);
    free(command_line);
    free(child_argv);
    if (!created) return make_result(-1, "", "failed to start process");
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return make_result((double)exit_code, "", "");
  }

  CapturePipe out_pipe;
  CapturePipe err_pipe;
  ZeroMemory(&out_pipe, sizeof(out_pipe));
  ZeroMemory(&err_pipe, sizeof(err_pipe));
  HANDLE out_write = NULL;
  HANDLE err_write = NULL;
  if (!create_capture_pipe(&out_pipe, &out_write) || !create_capture_pipe(&err_pipe, &err_write)) {
    if (out_write != NULL) CloseHandle(out_write);
    if (err_write != NULL) CloseHandle(err_write);
    close_capture_pipe(&out_pipe);
    close_capture_pipe(&err_pipe);
    free(command_line);
    free(child_argv);
    return make_result(-1, "", "could not create pipes");
  }
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = out_write;
  startup.hStdError = err_write;

  BOOL created = CreateProcessA(
    NULL, command_line, NULL, NULL, TRUE, 0, NULL, cwd, &startup, &process);
  free(command_line);
  free(child_argv);
  CloseHandle(out_write);
  CloseHandle(err_write);
  if (!created) {
    close_capture_pipe(&out_pipe);
    close_capture_pipe(&err_pipe);
    return make_result(-1, "", "failed to start process");
  }

  CloseHandle(process.hThread);
  HANDLE threads[2];
  threads[0] = CreateThread(NULL, 0, capture_thread, &out_pipe, 0, NULL);
  threads[1] = CreateThread(NULL, 0, capture_thread, &err_pipe, 0, NULL);
  for (int i = 0; i < 2; i++) {
    if (threads[i] != NULL) WaitForSingleObject(threads[i], INFINITE);
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hProcess);
  for (int i = 0; i < 2; i++) {
    if (threads[i] != NULL) CloseHandle(threads[i]);
  }

  xt_value result = make_result((double)exit_code, out_pipe.data, err_pipe.data);
  close_capture_pipe(&out_pipe);
  close_capture_pipe(&err_pipe);
  return result;
}

#else

static void append_chunk(char **buffer, size_t *length, size_t *capacity, const char *chunk, size_t count) {
  if (*length + count + 1 > *capacity) {
    size_t next = *capacity ? *capacity * 2 : 4096;
    while (next < *length + count + 1) next *= 2;
    char *grown = (char *)realloc(*buffer, next);
    if (!grown) return;
    *buffer = grown;
    *capacity = next;
  }
  memcpy(*buffer + *length, chunk, count);
  *length += count;
  (*buffer)[*length] = 0;
}

xt_value xt_child_process_spawn_sync(int32_t argc, xt_value *argv) {
  const char *command = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : NULL;
  if (!command) command = "";
  xt_value args = argc > 1 ? argv[1] : XT_UNDEFINED;
  const char *cwd = argc > 2 ? option_cwd(argv[2]) : NULL;

  int count = 0;
  char **child_argv = (char **)build_argv(command, args, &count);
  if (!child_argv) return make_result(-1, "", "out of memory");

  if (option_stdio_inherit(argc > 2 ? argv[2] : XT_UNDEFINED)) {
    pid_t inherit_pid = fork();
    if (inherit_pid < 0) {
      free(child_argv);
      return make_result(-1, "", "failed to fork");
    }
    if (inherit_pid == 0) {
      if (cwd) chdir(cwd);
      execvp(command, child_argv);
      _exit(127);
    }
    int inherit_status = 0;
    waitpid(inherit_pid, &inherit_status, 0);
    free(child_argv);
    return make_result(
      WIFEXITED(inherit_status) ? (double)WEXITSTATUS(inherit_status) : -1.0, "", "");
  }

  int out_pipe[2];
  int err_pipe[2];
  if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    free(child_argv);
    return make_result(-1, "", "could not create pipes");
  }

  pid_t pid = fork();
  if (pid == 0) {
    dup2(out_pipe[1], STDOUT_FILENO);
    dup2(err_pipe[1], STDERR_FILENO);
    close(out_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[0]);
    close(err_pipe[1]);
    if (cwd) chdir(cwd);
    execvp(command, child_argv);
    _exit(127);
  }
  close(out_pipe[1]);
  close(err_pipe[1]);

  int fds[2] = {out_pipe[0], err_pipe[0]};
  char *buffers[2] = {NULL, NULL};
  size_t lengths[2] = {0, 0};
  size_t capacities[2] = {0, 0};
  int live = 2;
  while (live > 0) {
    struct pollfd poll_fds[2];
    poll_fds[0].fd = fds[0];
    poll_fds[0].events = POLLIN;
    poll_fds[0].revents = 0;
    poll_fds[1].fd = fds[1];
    poll_fds[1].events = POLLIN;
    poll_fds[1].revents = 0;
    if (poll(poll_fds, 2, -1) < 0) break;
    for (int i = 0; i < 2; i++) {
      if (fds[i] < 0 || !(poll_fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
      char chunk[4096];
      ssize_t got = read(fds[i], chunk, sizeof(chunk));
      if (got > 0) {
        append_chunk(&buffers[i], &lengths[i], &capacities[i], chunk, (size_t)got);
      } else if (got == 0) {
        close(fds[i]);
        fds[i] = -1;
        live--;
      }
    }
  }

  int status = 0;
  waitpid(pid, &status, 0);
  double exit_code = WIFEXITED(status) ? (double)WEXITSTATUS(status) : -1.0;

  xt_value result = make_result(exit_code, buffers[0], buffers[1]);
  free(buffers[0]);
  free(buffers[1]);
  free(child_argv);
  return result;
}

#endif
