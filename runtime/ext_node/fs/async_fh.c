/*
 * `FileHandle` for `fs/promises`, backed by the async I/O pool.
 *
 * The handle is a plain object with an `fd` property and the usual methods;
 * each method marshals its arguments and submits an `XT_FS_JOB_FD_*` job, so a
 * read, write, fsync or close never blocks the reactor. `read` keeps the target
 * `Buffer` rooted in `roots[1]` while the worker fills a temporary byte array.
 */

#include "async_job.h"

#include <errno.h>

static int32_t xt_fh_fd(xt_value thisValue) { return (int32_t)xt_to_number(xt_object_get_cstr(thisValue, "fd")); }

static xt_value xt_fh_invalid_buffer(void) {
  return xt_promise_reject(xt_fs_error_object(EINVAL, "read", NULL));
}

static xt_value xt_fh_submit(xt_fs_job *job) { return xt_fs_job_submit(job); }

static xt_value xt_fh_read(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc < 1 || !xt_node_is_buffer(argv[0])) return xt_fh_invalid_buffer();
  xt_value buffer = argv[0];
  uint32_t bufferLength = xt_node_buffer_length(buffer);
  uint32_t offset = argc > 1 ? (uint32_t)xt_to_number(argv[1]) : 0;
  uint32_t length = argc > 2 ? (uint32_t)xt_to_number(argv[2]) : (bufferLength > offset ? bufferLength - offset : 0);
  int64_t position =
      argc > 3 && !XT_IS_UNDEFINED(argv[3]) && !XT_IS_NULL(argv[3]) ? (int64_t)xt_to_number(argv[3]) : -1;
  if (offset > bufferLength) offset = bufferLength;
  if (length > bufferLength - offset) length = bufferLength - offset;

  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_FD_READ);
  job->fd = xt_fh_fd(thisValue);
  job->offset = (long long)offset;
  job->length = (long long)length;
  job->position = (long long)position;
  job->base.roots[1] = buffer;
  return xt_fh_submit(job);
}

/* Copy `data` into a flat byte array the worker can read. */
static unsigned char *xt_fh_bytes(xt_value data, xt_value options, size_t *outLength, int *isBuffer) {
  if (xt_node_is_buffer(data)) {
    *isBuffer = 1;
    return xt_node_buffer_bytes(data, outLength);
  }
  *isBuffer = 0;
  xt_value text = xt_to_string(data);
  const char *bytes = xt_string_data(text);
  int32_t length = xt_string_length_value(text);
  if (!bytes || length < 0) {
    *outLength = 0;
    return NULL;
  }
  const char *encoding = xt_fs_encoding(options);
  return xt_fs_decode_text(bytes, (size_t)length, encoding, outLength);
}

static xt_value xt_fh_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  size_t length = 0;
  int isBuffer = 0;
  unsigned char *bytes = xt_fh_bytes(argv[0], argc > 2 ? argv[2] : XT_UNDEFINED, &length, &isBuffer);
  uint32_t offset = 0;
  uint32_t writeLength = (uint32_t)length;
  int64_t position = -1;
  if (isBuffer) {
    if (argc > 1 && !XT_IS_UNDEFINED(argv[1]) && !XT_IS_NULL(argv[1])) offset = (uint32_t)xt_to_number(argv[1]);
    if (argc > 2 && !XT_IS_UNDEFINED(argv[2]) && !XT_IS_NULL(argv[2])) writeLength = (uint32_t)xt_to_number(argv[2]);
    if (argc > 3 && !XT_IS_UNDEFINED(argv[3]) && !XT_IS_NULL(argv[3])) position = (int64_t)xt_to_number(argv[3]);
  } else if (argc > 1 && !XT_IS_UNDEFINED(argv[1]) && !XT_IS_NULL(argv[1])) {
    position = (int64_t)xt_to_number(argv[1]);
  }
  if (offset > length) offset = (uint32_t)length;
  if (writeLength > length - offset) writeLength = (uint32_t)length - offset;

  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_FD_WRITE);
  job->fd = xt_fh_fd(thisValue);
  job->position = (long long)position;
  if (writeLength > 0 && bytes) {
    job->data = (unsigned char *)malloc(writeLength);
    if (!job->data) abort();
    memcpy(job->data, bytes + offset, writeLength);
    job->data_length = writeLength;
  }
  free(bytes);
  return xt_fh_submit(job);
}

static xt_value xt_fh_write_file(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  size_t length = 0;
  int isBuffer = 0;
  unsigned char *bytes = xt_fh_bytes(argv[0], argc > 1 ? argv[1] : XT_UNDEFINED, &length, &isBuffer);
  (void)isBuffer;
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_FD_WRITE_FILE);
  job->fd = xt_fh_fd(thisValue);
  job->data = bytes;
  job->data_length = length;
  return xt_fh_submit(job);
}

static xt_value xt_fh_read_file(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_FD_READ_FILE);
  job->fd = xt_fh_fd(thisValue);
  job->text = xt_fs_copy_cstr(argc > 0 ? xt_fs_encoding(argv[0]) : NULL);
  return xt_fh_submit(job);
}

static xt_value xt_fh_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_FD_CLOSE);
  job->fd = xt_fh_fd(thisValue);
  return xt_fh_submit(job);
}

static xt_value xt_fh_stat(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_FD_STAT);
  job->fd = xt_fh_fd(thisValue);
  return xt_fh_submit(job);
}

static xt_value xt_fh_truncate(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_FD_TRUNCATE);
  job->fd = xt_fh_fd(thisValue);
  job->length = (long long)xt_to_number(argv[0]);
  return xt_fh_submit(job);
}

static xt_value xt_fh_chmod(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_FD_CHMOD);
  job->fd = xt_fh_fd(thisValue);
  job->mode = (int)xt_to_number(argv[0]);
  return xt_fh_submit(job);
}

static xt_value xt_fh_chown(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc < 2) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_FD_CHOWN);
  job->fd = xt_fh_fd(thisValue);
  job->uid = (int)xt_to_number(argv[0]);
  job->gid = (int)xt_to_number(argv[1]);
  return xt_fh_submit(job);
}

static xt_value xt_fh_utimes(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc < 2) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_FD_UTIMES);
  job->fd = xt_fh_fd(thisValue);
  job->atime = xt_fs_time_seconds(argv[0]);
  job->mtime = xt_fs_time_seconds(argv[1]);
  return xt_fh_submit(job);
}

static xt_value xt_fh_sync(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_FD_SYNC);
  job->fd = xt_fh_fd(thisValue);
  return xt_fh_submit(job);
}

static xt_value xt_fh_datasync(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_FD_DATASYNC);
  job->fd = xt_fh_fd(thisValue);
  return xt_fh_submit(job);
}

xt_value xt_fh_new(xt_value fdValue) {
  xt_value handle = xt_object_new();
  xt_node_set(handle, "fd", fdValue);
  xt_node_define_method(handle, "read", (void *)xt_fh_read);
  xt_node_define_method(handle, "write", (void *)xt_fh_write);
  xt_node_define_method(handle, "readFile", (void *)xt_fh_read_file);
  xt_node_define_method(handle, "writeFile", (void *)xt_fh_write_file);
  xt_node_define_method(handle, "appendFile", (void *)xt_fh_write_file);
  xt_node_define_method(handle, "close", (void *)xt_fh_close);
  xt_node_define_method(handle, "stat", (void *)xt_fh_stat);
  xt_node_define_method(handle, "truncate", (void *)xt_fh_truncate);
  xt_node_define_method(handle, "chmod", (void *)xt_fh_chmod);
  xt_node_define_method(handle, "chown", (void *)xt_fh_chown);
  xt_node_define_method(handle, "utimes", (void *)xt_fh_utimes);
  xt_node_define_method(handle, "sync", (void *)xt_fh_sync);
  xt_node_define_method(handle, "datasync", (void *)xt_fh_datasync);
  return handle;
}
