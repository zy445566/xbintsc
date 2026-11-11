/*
 * POSIX worker half of `child_process.spawn` / `execFile` (see spawn.c).
 *
 * `spawn` forks and `execvp`s the child; stdout/stderr are captured through
 * pipes drained with `poll(2)`. All of this runs on the async I/O pool thread,
 * so the blocking `waitpid`/`poll` never stalls the reactor.
 */

#include "spawn.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

/* -- argument / environment marshalling (main thread) --------------------- */

/* A NULL-terminated argv copy; `execvp` owns it once the child runs. */
static char **build_argv(const char *command, xt_value args) {
  int32_t count = XT_IS_ARRAY(args) ? xt_array_size(args) : 0;
  xt_value *items = XT_IS_ARRAY(args) ? xt_array_items(args) : NULL;
  char **argv = (char **)malloc(sizeof(char *) * (size_t)(count + 2));
  if (!argv) return NULL;
  argv[0] = spawn_join_string(command);
  if (!argv[0]) {
    free(argv);
    return NULL;
  }
  for (int32_t i = 0; i < count; i++) {
    argv[i + 1] = spawn_join_string(xt_node_cstr(items[i]));
    if (!argv[i + 1]) {
      for (int32_t j = 0; j <= i; j++) free(argv[j]);
      free(argv);
      return NULL;
    }
  }
  argv[count + 1] = NULL;
  return argv;
}

/* A NULL-terminated `envp` for `execvp`, or NULL when the child inherits. */
static char **build_envp(xt_value env) {
  if (!XT_IS_OBJECT(env)) return NULL;
  xt_value keys = xt_object_keys(env);
  int32_t count = XT_IS_ARRAY(keys) ? xt_array_size(keys) : 0;
  xt_value *items = XT_IS_ARRAY(keys) ? xt_array_items(keys) : NULL;
  char **envp = (char **)calloc((size_t)(count + 1), sizeof(char *));
  if (!envp) return NULL;
  int32_t kept = 0;
  for (int32_t i = 0; i < count; i++) {
    xt_value raw = xt_object_get(env, items[i]);
    if (XT_IS_UNDEFINED(raw)) continue;
    const char *name = xt_node_cstr(items[i]);
    const char *value = xt_node_cstr(raw);
    size_t length = strlen(name) + strlen(value) + 2;
    char *entry = (char *)malloc(length);
    if (!entry) continue;
    snprintf(entry, length, "%s=%s", name, value);
    envp[kept++] = entry;
  }
  envp[kept] = NULL;
  return envp;
}

int spawn_build_inputs(spawn_job *job, const char *command, xt_value args, xt_value options) {
  char **argv = build_argv(command, args);
  if (!argv) return 0;
  char **envp = NULL;
  if (XT_IS_OBJECT(options)) {
    xt_value env = xt_object_get_cstr(options, "env");
    envp = build_envp(env);
  }
  job->argv = argv;
  job->envp = envp;
  return 1;
}

/* -- capture (worker thread) ----------------------------------------------- */

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

void spawn_job_run(xt_async_job *base) {
  spawn_job *job = (spawn_job *)base;

  if (job->stdio_inherit) {
    pid_t pid = fork();
    if (pid < 0) {
      job->failed = 1;
      job->failed_message = "failed to fork";
      return;
    }
    if (pid == 0) {
      if (job->cwd) chdir(job->cwd);
      if (job->envp != NULL) {
        extern char **environ;
        environ = job->envp;
      }
      execvp(job->argv[0], job->argv);
      _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    job->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return;
  }

  int out_pipe[2];
  int err_pipe[2];
  if (job->stdio_ignore) {
    out_pipe[0] = out_pipe[1] = -1;
    err_pipe[0] = err_pipe[1] = -1;
  } else if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    job->failed = 1;
    job->failed_message = "could not create pipes";
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    if (!job->stdio_ignore) {
      close(out_pipe[0]);
      close(out_pipe[1]);
      close(err_pipe[0]);
      close(err_pipe[1]);
    }
    job->failed = 1;
    job->failed_message = "failed to fork";
    return;
  }
  if (pid == 0) {
    if (job->stdio_ignore) {
      int null_fd = open("/dev/null", O_RDWR);
      if (null_fd >= 0) {
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
        if (null_fd > 2) close(null_fd);
      }
    } else {
      dup2(out_pipe[1], STDOUT_FILENO);
      dup2(err_pipe[1], STDERR_FILENO);
      close(out_pipe[0]);
      close(out_pipe[1]);
      close(err_pipe[0]);
      close(err_pipe[1]);
    }
    if (job->cwd) chdir(job->cwd);
    if (job->envp != NULL) {
      extern char **environ;
      environ = job->envp;
    }
    execvp(job->argv[0], job->argv);
    _exit(127);
  }

  if (!job->stdio_ignore) {
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
    job->stdout_data = buffers[0];
    job->stdout_length = lengths[0];
    job->stderr_data = buffers[1];
    job->stderr_length = lengths[1];
  }

  int status = 0;
  waitpid(pid, &status, 0);
  job->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
