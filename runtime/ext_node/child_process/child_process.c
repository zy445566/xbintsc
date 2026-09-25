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

static char *read_temp(FILE *file) {
  fflush(file);
  if (_fseeki64(file, 0, SEEK_END) != 0) return NULL;
  long long size = _ftelli64(file);
  if (size < 0) return NULL;
  _fseeki64(file, 0, SEEK_SET);
  char *buffer = (char *)malloc((size_t)size + 1);
  if (!buffer) return NULL;
  size_t read = fread(buffer, 1, (size_t)size, file);
  buffer[read] = 0;
  return buffer;
}

xt_value xt_child_process_spawn_sync(int32_t argc, xt_value *argv) {
  const char *command = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : NULL;
  if (!command) command = "";
  xt_value args = argc > 1 ? argv[1] : XT_UNDEFINED;
  const char *cwd = argc > 2 ? option_cwd(argv[2]) : NULL;

  int count = 0;
  char **child_argv = (char **)build_argv(command, args, &count);
  if (!child_argv) return make_result(-1, "", "out of memory");

  char previous[4096];
  int had_cwd = cwd && _getcwd(previous, sizeof(previous)) != NULL;

  if (option_stdio_inherit(argc > 2 ? argv[2] : XT_UNDEFINED)) {
    if (cwd) _chdir(cwd);
    intptr_t inherit_rc = _spawnvp(_P_WAIT, command, (const char *const *)child_argv);
    if (had_cwd) _chdir(previous);
    free(child_argv);
    return make_result(inherit_rc < 0 ? -1 : (double)inherit_rc, "", "");
  }

  FILE *out_file = tmpfile();
  FILE *err_file = tmpfile();
  if (!out_file || !err_file) {
    free(child_argv);
    if (out_file) fclose(out_file);
    if (err_file) fclose(err_file);
    return make_result(-1, "", "could not create temporary files");
  }

  /* Flush anything the parent has already buffered on stdout/stderr before
   * redirecting the descriptors; otherwise that pending output would be
   * flushed into the child's capture file (and lost from the real stdout). */
  fflush(stdout);
  fflush(stderr);
  int saved_out = _dup(_fileno(stdout));
  int saved_err = _dup(_fileno(stderr));
  _dup2(_fileno(out_file), _fileno(stdout));
  _dup2(_fileno(err_file), _fileno(stderr));

  if (cwd) _chdir(cwd);
  intptr_t rc = _spawnvp(_P_WAIT, command, (const char *const *)child_argv);
  if (had_cwd) _chdir(previous);
  fflush(stdout);
  fflush(stderr);
  _dup2(saved_out, _fileno(stdout));
  _dup2(saved_err, _fileno(stderr));
  _close(saved_out);
  _close(saved_err);

  char *out = read_temp(out_file);
  char *err = read_temp(err_file);
  fclose(out_file);
  fclose(err_file);
  free(child_argv);

  xt_value result = make_result(rc < 0 ? -1 : (double)rc, out, err);
  free(out);
  free(err);
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
