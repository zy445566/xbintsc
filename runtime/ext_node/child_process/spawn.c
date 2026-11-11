/*
 * Node.js `child_process.spawn` for xbintsc (Windows).
 *
 * `spawn` launches a child process asynchronously: the capture (or pass-through,
 * for `stdio: "inherit"`) runs on the async I/O pool, and the completion
 * handler delivers stdout/stderr chunks and the `close`/`exit`/`error` events on
 * the reactor thread. A ChildProcess is a plain EventEmitter-like object whose
 * `stdout`/`stderr` are Readable streams from the `stream` extension.
 */

#include "../node_common.h"

#include <windows.h>

xt_value xt_stream_deliver(xt_value stream, xt_value chunk);
xt_value xt_stream_new_readable(void);
static xt_value xt_child_process_proto(void);

typedef struct {
  xt_async_job base; /* must stay first */
  char *command_line;
  char *cwd;
  char *env_block;
  int stdio_inherit;
  int stdio_ignore;
  int exec_file;
  int exit_code;
  int failed;
  char *failed_message;
  char *stdout_data;
  size_t stdout_length;
  char *stderr_data;
  size_t stderr_length;
} spawn_job;

/* -- argument / option marshalling (main thread) --------------------------- */

static char *join_string(const char *text) {
  if (!text) return NULL;
  size_t length = strlen(text);
  char *copy = (char *)malloc(length + 1);
  if (!copy) return NULL;
  memcpy(copy, text, length + 1);
  return copy;
}

/* Quote one argument following CommandLineToArgvW/MSVC rules. */
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

static char *build_command_line(const char *command, xt_value args) {
  int32_t count = XT_IS_ARRAY(args) ? xt_array_size(args) : 0;
  xt_value *items = XT_IS_ARRAY(args) ? xt_array_items(args) : NULL;
  size_t capacity = strlen(command) * 2 + 4;
  for (int32_t i = 0; i < count; i++) capacity += strlen(xt_node_cstr(items[i])) * 2 + 4;
  char *line = (char *)malloc(capacity);
  if (!line) return NULL;
  char *cursor = line;
  cursor = append_quoted_argument(cursor, command);
  for (int32_t i = 0; i < count; i++) {
    *cursor++ = ' ';
    cursor = append_quoted_argument(cursor, xt_node_cstr(items[i]));
  }
  *cursor = 0;
  return line;
}

static int compare_env_entries(const void *left, const void *right) {
  const char *a = *(const char *const *)left;
  const char *b = *(const char *const *)right;
  if (a == NULL) return b == NULL ? 0 : 1;
  if (b == NULL) return -1;
  return _stricmp(a, b);
}

static char *build_env_block(xt_value env) {
  if (!XT_IS_OBJECT(env)) return NULL;
  xt_value keys = xt_object_keys(env);
  int32_t count = XT_IS_ARRAY(keys) ? xt_array_size(keys) : 0;
  xt_value *items = XT_IS_ARRAY(keys) ? xt_array_items(keys) : NULL;
  char **entries = (char **)calloc((size_t)(count > 0 ? count : 1), sizeof(char *));
  if (!entries) return NULL;
  for (int32_t i = 0; i < count; i++) {
    xt_value raw = xt_object_get(env, items[i]);
    if (XT_IS_UNDEFINED(raw)) continue;
    const char *name = xt_node_cstr(items[i]);
    const char *value = xt_node_cstr(raw);
    size_t length = strlen(name) + strlen(value) + 2;
    entries[i] = (char *)malloc(length);
    if (entries[i]) snprintf(entries[i], length, "%s=%s", name, value);
  }
  qsort(entries, (size_t)count, sizeof(char *), compare_env_entries);
  size_t total = 1;
  for (int32_t i = 0; i < count; i++) total += entries[i] ? strlen(entries[i]) + 1 : 0;
  char *block = (char *)malloc(total + 1);
  if (!block) {
    for (int32_t i = 0; i < count; i++) free(entries[i]);
    free(entries);
    return NULL;
  }
  char *cursor = block;
  for (int32_t i = 0; i < count; i++) {
    if (!entries[i]) continue;
    size_t length = strlen(entries[i]) + 1;
    memcpy(cursor, entries[i], length);
    cursor += length;
  }
  *cursor++ = 0;
  *cursor = 0;
  for (int32_t i = 0; i < count; i++) free(entries[i]);
  free(entries);
  return block;
}

/* -- capture (worker thread) ----------------------------------------------- */

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

static void spawn_job_run(xt_async_job *base) {
  spawn_job *job = (spawn_job *)base;
  STARTUPINFOA startup;
  PROCESS_INFORMATION process;
  ZeroMemory(&startup, sizeof(startup));
  ZeroMemory(&process, sizeof(process));
  startup.cb = sizeof(startup);

  if (job->stdio_inherit) {
    BOOL created = CreateProcessA(NULL, job->command_line, NULL, NULL, TRUE, 0, job->env_block, job->cwd, &startup, &process);
    if (!created) {
      job->failed = 1;
      job->failed_message = "failed to start process";
      return;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    job->exit_code = (int)exit_code;
    return;
  }

  CapturePipe out_pipe;
  CapturePipe err_pipe;
  ZeroMemory(&out_pipe, sizeof(out_pipe));
  ZeroMemory(&err_pipe, sizeof(err_pipe));
  HANDLE out_write = NULL;
  HANDLE err_write = NULL;
  if (job->stdio_ignore) {
    out_pipe.read = NULL;
    err_pipe.read = NULL;
    out_write = NULL;
    err_write = NULL;
  } else if (!create_capture_pipe(&out_pipe, &out_write) || !create_capture_pipe(&err_pipe, &err_write)) {
    if (out_write != NULL) CloseHandle(out_write);
    if (err_write != NULL) CloseHandle(err_write);
    close_capture_pipe(&out_pipe);
    close_capture_pipe(&err_pipe);
    job->failed = 1;
    job->failed_message = "could not create pipes";
    return;
  }

  if (job->stdio_ignore) {
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = NULL;
    startup.hStdOutput = NULL;
    startup.hStdError = NULL;
  } else {
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = out_write;
    startup.hStdError = err_write;
  }

  BOOL created = CreateProcessA(NULL, job->command_line, NULL, NULL, TRUE, 0, job->env_block, job->cwd, &startup, &process);
  if (out_write != NULL) CloseHandle(out_write);
  if (err_write != NULL) CloseHandle(err_write);
  if (!created) {
    close_capture_pipe(&out_pipe);
    close_capture_pipe(&err_pipe);
    job->failed = 1;
    job->failed_message = "failed to start process";
    return;
  }

  CloseHandle(process.hThread);
  HANDLE threads[2] = {NULL, NULL};
  if (!job->stdio_ignore) {
    threads[0] = CreateThread(NULL, 0, capture_thread, &out_pipe, 0, NULL);
    threads[1] = CreateThread(NULL, 0, capture_thread, &err_pipe, 0, NULL);
    for (int i = 0; i < 2; i++) {
      if (threads[i] != NULL) WaitForSingleObject(threads[i], INFINITE);
    }
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hProcess);
  for (int i = 0; i < 2; i++) {
    if (threads[i] != NULL) CloseHandle(threads[i]);
  }

  job->exit_code = (int)exit_code;
  if (!job->stdio_ignore) {
    job->stdout_data = out_pipe.data;
    job->stdout_length = out_pipe.length;
    out_pipe.data = NULL;
    job->stderr_data = err_pipe.data;
    job->stderr_length = err_pipe.length;
    err_pipe.data = NULL;
  }
  close_capture_pipe(&out_pipe);
  close_capture_pipe(&err_pipe);
}

/* -- completion (reactor thread) ------------------------------------------ */

static void spawn_job_complete(xt_async_job *base) {
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

static void spawn_job_release(xt_async_job *base) {
  spawn_job *job = (spawn_job *)base;
  free(job->command_line);
  free(job->cwd);
  free(job->env_block);
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

  spawn_job *job = (spawn_job *)calloc(1, sizeof(spawn_job));
  if (!job) return XT_UNDEFINED;
  job->base.run = spawn_job_run;
  job->base.complete = spawn_job_complete;
  job->base.release = spawn_job_release;

  job->command_line = build_command_line(command, args);
  if (!job->command_line) {
    free(job);
    return XT_UNDEFINED;
  }

  if (XT_IS_OBJECT(options)) {
    xt_value cwd = xt_object_get_cstr(options, "cwd");
    if (XT_IS_STRING(cwd)) job->cwd = join_string(xt_string_data(cwd));
    xt_value env = xt_object_get_cstr(options, "env");
    job->env_block = build_env_block(env);
    xt_value stdio = xt_object_get_cstr(options, "stdio");
    if (XT_IS_STRING(stdio)) {
      const char *mode = xt_string_data(stdio);
      if (strcmp(mode, "inherit") == 0) job->stdio_inherit = 1;
      else if (strcmp(mode, "ignore") == 0) job->stdio_ignore = 1;
    }
  }

  xt_value stdout_stream = xt_stream_new_readable();
  xt_value stderr_stream = xt_stream_new_readable();
  xt_value child = xt_child_process_new(stdout_stream, stderr_stream);
  job->base.roots[0] = child;
  job->base.roots[1] = stdout_stream;
  job->base.roots[2] = stderr_stream;

  xt_async_submit(&job->base);
  return child;
}

xt_value xt_child_process_spawn_sync(int32_t argc, xt_value *argv);

/* `execFile(file, args, options, callback)`: async, no shell, callback gets
 * (error, stdout). Reuses the spawn capture with the callback as a root. */
xt_value xt_child_process_exec_file(int32_t argc, xt_value *argv) {
  const char *file = argc > 0 ? xt_node_cstr(argv[0]) : "";
  xt_value args = argc > 1 ? argv[1] : XT_UNDEFINED;
  xt_value options = argc > 2 ? argv[2] : XT_UNDEFINED;
  xt_value callback = argc > 3 ? argv[3] : XT_UNDEFINED;

  spawn_job *job = (spawn_job *)calloc(1, sizeof(spawn_job));
  if (!job) return XT_UNDEFINED;
  job->base.run = spawn_job_run;
  job->base.complete = spawn_job_complete;
  job->base.release = spawn_job_release;
  job->exec_file = 1;

  job->command_line = build_command_line(file, args);
  if (!job->command_line) {
    free(job);
    return XT_UNDEFINED;
  }
  if (XT_IS_OBJECT(options)) {
    xt_value cwd = xt_object_get_cstr(options, "cwd");
    if (XT_IS_STRING(cwd)) job->cwd = join_string(xt_string_data(cwd));
    xt_value env = xt_object_get_cstr(options, "env");
    job->env_block = build_env_block(env);
    xt_value stdio = xt_object_get_cstr(options, "stdio");
    if (XT_IS_STRING(stdio)) {
      const char *mode = xt_string_data(stdio);
      if (strcmp(mode, "inherit") == 0) job->stdio_inherit = 1;
      else if (strcmp(mode, "ignore") == 0) job->stdio_ignore = 1;
    }
  }

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
