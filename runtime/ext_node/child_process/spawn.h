/*
 * Shared job model for the asynchronous `child_process.spawn` / `execFile`
 * implementation.
 *
 * The worker half (`spawn_win.c` / `spawn_posix.c`) launches the child and
 * captures stdout/stderr on the async I/O pool; the main-thread half
 * (`spawn.c`) turns those results into `xt_value`s, emits the stream/`exit`/
 * `close` events and frees the job. Exactly one platform file is compiled per
 * host, selected by the extension's `runtimeSources()`.
 */
#ifndef XT_NODE_CHILD_PROCESS_SPAWN_H
#define XT_NODE_CHILD_PROCESS_SPAWN_H

#include "../node_common.h"

typedef struct spawn_job {
  xt_async_job base; /* must stay first */
  char *cwd;
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
#if defined(_WIN32)
  char *command_line;
  char *env_block;
#else
  char **argv;
  char **envp;
#endif
} spawn_job;

/* Copy a NUL-terminated string; NULL stays NULL. */
static inline char *spawn_join_string(const char *text) {
  if (!text) return NULL;
  size_t length = strlen(text);
  char *copy = (char *)malloc(length + 1);
  if (!copy) return NULL;
  memcpy(copy, text, length + 1);
  return copy;
}

/* Shared orchestration (spawn.c). */
void spawn_job_complete(xt_async_job *base);
void spawn_job_release(xt_async_job *base);

/* Platform worker + input marshalling (spawn_win.c / spawn_posix.c). */
void spawn_job_run(xt_async_job *base);
int spawn_build_inputs(spawn_job *job, const char *command, xt_value args, xt_value options);

#endif /* XT_NODE_CHILD_PROCESS_SPAWN_H */
