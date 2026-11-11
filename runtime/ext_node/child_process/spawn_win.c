/*
 * Windows worker half of `child_process.spawn` / `execFile` (see spawn.c).
 *
 * `spawn` launches the child with `CreateProcessA`; stdout/stderr are drained
 * through anonymous pipes by reader threads, so a child writing more than a
 * pipe buffer cannot deadlock. All of this runs on the async I/O pool thread.
 */

#include "spawn.h"

#include <windows.h>

/* -- argument / environment marshalling (main thread) --------------------- */

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

int spawn_build_inputs(spawn_job *job, const char *command, xt_value args, xt_value options) {
  char *command_line = build_command_line(command, args);
  if (!command_line) return 0;
  char *env_block = NULL;
  if (XT_IS_OBJECT(options)) {
    xt_value env = xt_object_get_cstr(options, "env");
    env_block = build_env_block(env);
  }
  job->command_line = command_line;
  job->env_block = env_block;
  return 1;
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

void spawn_job_run(xt_async_job *base) {
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
