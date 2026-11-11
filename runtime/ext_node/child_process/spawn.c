/*
 * Node.js `child_process.spawn` / `execFile` / `execSync` for xbintsc.
 *
 * `spawn` and `execFile` launch a child process asynchronously: the capture (or
 * pass-through, for `stdio: "inherit"`) runs on the async I/O pool (the worker
 * half lives in `spawn_win.c` / `spawn_posix.c`), and the completion handler
 * here delivers stdout/stderr chunks and the `close`/`exit`/`error` events on
 * the reactor thread. A ChildProcess is a plain EventEmitter-like object whose
 * `stdout`/`stderr` are Readable streams from the `stream` extension.
 *
 * `execSync` runs through the platform shell (`cmd /c` on Windows, `/bin/sh -c`
 * elsewhere) and delegates to the synchronous `spawnSync` in child_process.c.
 */

#include "spawn.h"

xt_value xt_stream_deliver(xt_value stream, xt_value chunk);
xt_value xt_stream_new_readable(void);
xt_value xt_child_process_spawn_sync(int32_t argc, xt_value *argv);
static xt_value xt_child_process_proto(void);

/* -- job construction (main thread) ---------------------------------------- */

static spawn_job *spawn_job_new(const char *command, xt_value args, xt_value options) {
  spawn_job *job = (spawn_job *)calloc(1, sizeof(spawn_job));
  if (!job) return NULL;
  job->base.run = spawn_job_run;
  job->base.complete = spawn_job_complete;
  job->base.release = spawn_job_release;

  if (XT_IS_OBJECT(options)) {
    xt_value cwd = xt_object_get_cstr(options, "cwd");
    if (XT_IS_STRING(cwd)) job->cwd = spawn_join_string(xt_string_data(cwd));
    xt_value stdio = xt_object_get_cstr(options, "stdio");
    if (XT_IS_STRING(stdio)) {
      const char *mode = xt_string_data(stdio);
      if (strcmp(mode, "inherit") == 0) job->stdio_inherit = 1;
      else if (strcmp(mode, "ignore") == 0) job->stdio_ignore = 1;
    }
  }

  if (!spawn_build_inputs(job, command, args, options)) {
    spawn_job_release(&job->base);
    return NULL;
  }
  return job;
}

/* -- completion (reactor thread) ------------------------------------------ */

void spawn_job_complete(xt_async_job *base) {
  spawn_job *job = (spawn_job *)base;
  if (job->exec_file) {
    xt_value callback = job->base.roots[3];
    if (XT_IS_FUNCTION(callback)) {
      if (job->failed) {
        xt_value error = xt_object_new();
        xt_node_set(error, "message", xt_string_from_cstr(job->failed_message ? job->failed_message : "spawn failed"));
        xt_value args[2] = { error, xt_string_from_cstr("") };
        xt_call_with_this(callback, XT_UNDEFINED, 2, args);
      } else {
        xt_value out = xt_string_new(job->stdout_data ? job->stdout_data : "", job->stdout_length);
        xt_value args[2] = { XT_NULL, out };
        xt_call_with_this(callback, XT_UNDEFINED, 2, args);
      }
    }
    return;
  }

  xt_value child = job->base.roots[0];
  xt_value stdout_stream = job->base.roots[1];
  xt_value stderr_stream = job->base.roots[2];

  if (job->failed) {
    xt_value error = xt_object_new();
    xt_node_set(error, "message", xt_string_from_cstr(job->failed_message ? job->failed_message : "spawn failed"));
    xt_node_emit1(child, "error", error);
    return;
  }

  if (job->stdio_inherit) {
    xt_node_emit1(child, "exit", xt_number((double)job->exit_code));
    xt_node_emit1(child, "close", xt_number((double)job->exit_code));
    return;
  }
  if (job->stdio_ignore) {
    xt_node_emit1(child, "exit", xt_number((double)job->exit_code));
    xt_node_emit1(child, "close", xt_number((double)job->exit_code));
    return;
  }

  xt_value out = xt_string_new(job->stdout_data ? job->stdout_data : "", job->stdout_length);
  xt_value err = xt_string_new(job->stderr_data ? job->stderr_data : "", job->stderr_length);
  if (job->stdout_length > 0) xt_stream_deliver(stdout_stream, out);
  if (job->stderr_length > 0) xt_stream_deliver(stderr_stream, err);
  xt_node_emit1(child, "exit", xt_number((double)job->exit_code));
  xt_node_emit1(child, "close", xt_number((double)job->exit_code));
}

void spawn_job_release(xt_async_job *base) {
  spawn_job *job = (spawn_job *)base;
  free(job->cwd);
#if defined(_WIN32)
  free(job->command_line);
  free(job->env_block);
#else
  if (job->argv != NULL) {
    for (char **entry = job->argv; *entry != NULL; entry++) free(*entry);
    free(job->argv);
  }
  if (job->envp != NULL) {
    for (char **entry = job->envp; *entry != NULL; entry++) free(*entry);
    free(job->envp);
  }
#endif
  free(job->stdout_data);
  free(job->stderr_data);
  free(job);
}

/* -- ChildProcess construction (main thread) ------------------------------ */

static xt_value xt_child_process_new(xt_value stdout_stream, xt_value stderr_stream) {
  xt_value child = xt_object_new_with_proto(xt_child_process_proto());
  xt_node_set(child, "stdout", stdout_stream);
  xt_node_set(child, "stderr", stderr_stream);
  xt_node_set(child, "stdin", xt_stream_new_readable());
  return child;
}

xt_value xt_child_process_spawn(int32_t argc, xt_value *argv) {
  const char *command = argc > 0 ? xt_node_cstr(argv[0]) : "";
  xt_value args = argc > 1 ? argv[1] : XT_UNDEFINED;
  xt_value options = argc > 2 ? argv[2] : XT_UNDEFINED;

  spawn_job *job = spawn_job_new(command, args, options);
  if (!job) return XT_UNDEFINED;

  xt_value stdout_stream = xt_stream_new_readable();
  xt_value stderr_stream = xt_stream_new_readable();
  xt_value child = xt_child_process_new(stdout_stream, stderr_stream);
  job->base.roots[0] = child;
  job->base.roots[1] = stdout_stream;
  job->base.roots[2] = stderr_stream;

  xt_async_submit(&job->base);
  return child;
}

/* `execFile(file, args, options, callback)`: async, no shell, callback gets
 * (error, stdout). Reuses the spawn capture with the callback as a root. */
xt_value xt_child_process_exec_file(int32_t argc, xt_value *argv) {
  const char *file = argc > 0 ? xt_node_cstr(argv[0]) : "";
  xt_value args = argc > 1 ? argv[1] : XT_UNDEFINED;
  xt_value options = argc > 2 ? argv[2] : XT_UNDEFINED;
  xt_value callback = argc > 3 ? argv[3] : XT_UNDEFINED;

  spawn_job *job = spawn_job_new(file, args, options);
  if (!job) return XT_UNDEFINED;
  job->exec_file = 1;
  job->base.roots[3] = callback;
  xt_async_submit(&job->base);
  return XT_UNDEFINED;
}

/* `execSync(command, options)`: synchronous, runs through the platform shell and
 * returns the captured stdout string. */
xt_value xt_child_process_exec_sync(int32_t argc, xt_value *argv) {
  const char *command = argc > 0 ? xt_node_cstr(argv[0]) : "";
  xt_value options = argc > 1 ? argv[1] : XT_UNDEFINED;
#if defined(_WIN32)
  const char *shell = "cmd";
  xt_value shell_args[2] = { xt_string_from_cstr("/c"), xt_string_from_cstr(command) };
#else
  const char *shell = "/bin/sh";
  xt_value shell_args[2] = { xt_string_from_cstr("-c"), xt_string_from_cstr(command) };
#endif
  xt_value args_array = xt_array_new(2, shell_args);
  xt_value spawn_argv[3] = { xt_string_from_cstr(shell), args_array, options };
  xt_value result = xt_child_process_spawn_sync(3, spawn_argv);
  return xt_object_get_cstr(result, "stdout");
}

/* -- ChildProcess prototype ------------------------------------------------ */

static xt_value xt_child_process_kill(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  return xt_bool(1);
}

static xt_value xt_child_process_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "kill", (void *)xt_child_process_kill);
  return proto;
}
