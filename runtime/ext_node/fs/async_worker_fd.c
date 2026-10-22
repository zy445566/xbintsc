/*
 * Descriptor read/write worker jobs.
 *
 * Split out of async_worker.c to keep each translation unit small. These only
 * touch the descriptor and a plain byte buffer; the completion handler copies
 * the bytes into the caller's `Buffer` on the main thread.
 */

#include "async_worker_dir.h"

#include <errno.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

void xt_fs_worker_fd_read(xt_fs_job *job) {
  size_t length = (size_t)job->length;
  if (length == 0) {
    job->bytes = NULL;
    job->bytes_length = 0;
    return;
  }
  unsigned char *bytes = (unsigned char *)malloc(length);
  if (!bytes) {
    xt_fs_job_fail(job, ENOMEM, "read", NULL);
    return;
  }
  ssize_t count;
  if (job->position >= 0) {
#if defined(_WIN32)
    _lseeki64(job->fd, job->position, SEEK_SET);
    count = _read(job->fd, bytes, (unsigned int)length);
#else
    count = pread(job->fd, bytes, length, (off_t)job->position);
#endif
  } else {
    count = XT_FS_READ(job->fd, bytes, length);
  }
  if (count < 0) {
    int saved = errno;
    free(bytes);
    xt_fs_job_fail(job, saved, "read", NULL);
    return;
  }
  job->bytes = bytes;
  job->bytes_length = (size_t)count;
}

void xt_fs_worker_fd_write(xt_fs_job *job) {
  if (job->data_length == 0) {
    job->written = 0;
    return;
  }
  ssize_t count;
  if (job->position >= 0) {
#if defined(_WIN32)
    _lseeki64(job->fd, job->position, SEEK_SET);
    count = _write(job->fd, job->data, (unsigned int)job->data_length);
#else
    count = pwrite(job->fd, job->data, job->data_length, (off_t)job->position);
#endif
  } else {
    count = XT_FS_WRITE(job->fd, job->data, job->data_length);
  }
  if (count < 0) {
    xt_fs_job_fail(job, errno, "write", NULL);
    return;
  }
  job->written = (long long)count;
}

