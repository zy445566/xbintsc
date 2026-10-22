/*
 * Lifecycle helpers for asynchronous `fs/promises` jobs.
 *
 * Allocation, submission, failure bookkeeping and release are shared by the
 * argument marshalling (async_api.c), the FileHandle surface (async_fh.c), the
 * worker (async_worker.c) and the completion handler (async_complete.c).
 */

#include "async_job.h"

#include <errno.h>

char *xt_fs_copy_cstr(const char *text) {
  if (!text) return NULL;
  size_t length = strlen(text);
  char *copy = (char *)malloc(length + 1);
  if (!copy) return NULL;
  memcpy(copy, text, length + 1);
  return copy;
}

char *xt_fs_take_string(xt_value value) {
  if (XT_IS_UNDEFINED(value) || XT_IS_NULL(value)) return NULL;
  xt_value text = xt_to_string(value);
  const char *data = xt_string_data(text);
  if (!data) return NULL;
  size_t length = (size_t)xt_string_length_value(text);
  char *copy = (char *)malloc(length + 1);
  if (!copy) return NULL;
  memcpy(copy, data, length);
  copy[length] = '\0';
  return copy;
}

xt_fs_job *xt_fs_job_new(xt_fs_job_kind kind) {
  xt_fs_job *job = (xt_fs_job *)calloc(1, sizeof(xt_fs_job));
  if (!job) abort();
  job->base.run = xt_fs_job_run;
  job->base.complete = xt_fs_job_complete;
  job->base.release = xt_fs_job_release;
  job->kind = kind;
  job->fd = -1;
  return job;
}

xt_value xt_fs_job_submit(xt_fs_job *job) {
  xt_value promise = xt_promise_create();
  job->base.roots[0] = promise;
  xt_async_submit(&job->base);
  return promise;
}

/* Record the errno of a just-failed worker syscall. `error_path` aliases a
 * job-owned string, so release() must not free it separately. */
void xt_fs_job_fail(xt_fs_job *job, int err, const char *syscall, const char *path) {
  job->err = err ? err : EIO;
  job->syscall = syscall;
  job->error_path = (char *)path;
}

void xt_fs_job_release(xt_async_job *base) {
  xt_fs_job *job = (xt_fs_job *)base;
  free(job->path);
  free(job->path2);
  free(job->text);
  if (job->patterns) {
    for (size_t i = 0; i < job->pattern_count; i++) free(job->patterns[i]);
    free(job->patterns);
  }
  free(job->data);
  free(job->bytes);
  free(job->string);
  if (job->entries) {
    for (size_t i = 0; i < job->entry_count; i++) {
      free(job->entries[i].name);
      if (job->entries[i].bare != job->entries[i].name) free(job->entries[i].bare);
    }
    free(job->entries);
  }
  free(job);
}
