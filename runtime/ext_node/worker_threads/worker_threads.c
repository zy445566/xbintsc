/*
 * Node.js `worker_threads` module for xbintsc.
 *
 * xbintsc has no threads, so a worker is emulated by re-running the current
 * executable as a child process. `isMainThread`/`workerData` read the
 * `XBINTSC_WORKER`/`XBINTSC_WORKER_DATA` environment variables the parent sets,
 * and `parentPort.postMessage(x)` writes `x` to the child's stdout, which the
 * parent captures and re-emits as a `"message"` event on the `Worker` object.
 * Because the event emitter buffers events until a listener is attached,
 * `new Worker(...)` can run the child synchronously and still deliver the
 * message to a later `worker.on("message", ...)`.
 */

#include "../node_common.h"

#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

/* -- environment ---------------------------------------------------------- */

static int xt_worker_is_worker(void) {
  return getenv("XBINTSC_WORKER") != NULL;
}

static void xt_worker_set_env(const char *key, const char *value) {
#if defined(_WIN32)
  _putenv_s(key, value ? value : "");
#else
  if (value) setenv(key, value, 1);
  else unsetenv(key);
#endif
}

/* -- value bindings ------------------------------------------------------- */

xt_value xt_worker_is_main_thread(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_bool(!xt_worker_is_worker());
}

xt_value xt_worker_data(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  const char *json = getenv("XBINTSC_WORKER_DATA");
  if (!json) return xt_undefined();
  xt_value text = xt_string_from_cstr(json);
  return xt_json_parse(1, &text);
}

static xt_value xt_worker_port_post(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  xt_value message = xt_arg(argc, argv, 0);
  xt_value text = xt_to_string(message);
  const char *data = xt_string_data(text);
  if (data) {
    fwrite(data, 1, strlen(data), stdout);
  }
  fputc('\n', stdout);
  fflush(stdout);
  return xt_undefined();
}

xt_value xt_worker_parent_port(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value port = xt_object_new();
  xt_node_set(port, "_kind", xt_string_from_cstr("parentPort"));
  xt_node_set(port, "postMessage", xt_closure_new((void *)xt_worker_port_post, 0, NULL));
  return port;
}

/* -- child process -------------------------------------------------------- */

/* Run `path` as a worker and capture its stdout. Returns a heap string (caller
 * frees) or NULL on failure. */
static char *xt_worker_spawn(const char *path, const char *workerDataJson, int *exitCode) {
  *exitCode = -1;
#if defined(_WIN32)
  char previous[4096];
  int had_cwd = _getcwd(previous, sizeof(previous)) != NULL;
  (void)had_cwd;
  FILE *capture = tmpfile();
  if (!capture) return NULL;
  int saved = _dup(1);
  if (saved < 0) {
    fclose(capture);
    return NULL;
  }
  fflush(stdout);
  _dup2(_fileno(capture), 1);
  xt_worker_set_env("XBINTSC_WORKER", "1");
  if (workerDataJson) xt_worker_set_env("XBINTSC_WORKER_DATA", workerDataJson);
  intptr_t rc = _spawnlp(_P_WAIT, path, path, (char *)NULL);
  fflush(stdout);
  _dup2(saved, 1);
  _close(saved);
  xt_worker_set_env("XBINTSC_WORKER", NULL);
  xt_worker_set_env("XBINTSC_WORKER_DATA", NULL);
  if (rc < 0) {
    fclose(capture);
    return NULL;
  }
  *exitCode = (int)rc;
  if (_fseeki64(capture, 0, SEEK_END) != 0) {
    fclose(capture);
    return NULL;
  }
  long long size = _ftelli64(capture);
  _fseeki64(capture, 0, SEEK_SET);
  char *buffer = (char *)malloc((size_t)size + 1);
  if (!buffer) {
    fclose(capture);
    return NULL;
  }
  size_t got = fread(buffer, 1, (size_t)size, capture);
  buffer[got] = 0;
  fclose(capture);
  return buffer;
#else
  if (workerDataJson) xt_worker_set_env("XBINTSC_WORKER_DATA", workerDataJson);
  xt_worker_set_env("XBINTSC_WORKER", "1");
  int fds[2];
  if (pipe(fds) != 0) return NULL;
  pid_t pid = fork();
  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    close(fds[0]);
    close(fds[1]);
    char *childArgv[2];
    childArgv[0] = (char *)path;
    childArgv[1] = NULL;
    execv(path, childArgv);
    _exit(127);
  }
  close(fds[1]);
  char *buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chunk[4096];
  ssize_t got;
  while ((got = read(fds[0], chunk, sizeof(chunk))) > 0) {
    char *grown = (char *)realloc(buffer, length + (size_t)got + 1);
    if (!grown) break;
    buffer = grown;
    memcpy(buffer + length, chunk, (size_t)got);
    length += (size_t)got;
    buffer[length] = 0;
  }
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  *exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  xt_worker_set_env("XBINTSC_WORKER", NULL);
  xt_worker_set_env("XBINTSC_WORKER_DATA", NULL);
  if (!buffer) buffer = (char *)calloc(1, 1);
  (void)capacity;
  return buffer;
#endif
}

xt_value xt_worker_ctor(int32_t argc, xt_value *argv) {
  xt_value options = argc > 1 ? argv[1] : XT_UNDEFINED;
  xt_value data = XT_IS_OBJECT(options) ? xt_object_get_cstr(options, "workerData") : XT_UNDEFINED;

  const char *path = (xt_program_argv && xt_program_argv[0]) ? xt_program_argv[0] : NULL;
  char *workerDataJson = NULL;
  if (data != XT_UNDEFINED) {
    xt_value json = xt_json_stringify(1, &data);
    const char *text = xt_string_data(json);
    if (text) {
      size_t size = strlen(text);
      workerDataJson = (char *)malloc(size + 1);
      if (workerDataJson) memcpy(workerDataJson, text, size + 1);
    }
  }

  xt_value worker = xt_object_new();
  xt_node_set(worker, "_kind", xt_string_from_cstr("worker"));
  xt_node_install_emitter(worker);
  xt_node_set(worker, "postMessage", xt_closure_new((void *)xt_worker_port_post, 0, NULL));

  int exitCode = -1;
  char *output = path ? xt_worker_spawn(path, workerDataJson, &exitCode) : NULL;
  free(workerDataJson);

  if (output) {
    size_t length = strlen(output);
    while (length > 0 && (output[length - 1] == '\n' || output[length - 1] == '\r')) output[--length] = 0;
    xt_node_emit1(worker, "message", xt_string_new(output, length));
    free(output);
  } else {
    xt_node_emit1(worker, "error", xt_string_from_cstr("failed to start worker"));
  }
  xt_node_emit1(worker, "exit", xt_number((double)exitCode));
  return worker;
}
