/*
 * Shared job model for the asynchronous `fs/promises` implementation.
 *
 * The promise API deliberately splits work in two halves:
 *
 *   - the worker half (async_worker.c) performs blocking syscalls and writes
 *     plain C results into `xt_fs_job`;
 *   - the main-thread half (async_complete.c) turns those results into
 *     `xt_value`s, settles the promise and frees the job.
 *
 * Argument marshalling lives in async_api.c and the FileHandle surface in
 * async_fh.c. Keeping the split explicit is what lets `await` yield while a
 * large read or a slow network filesystem is in flight.
 */
#ifndef XT_NODE_FS_ASYNC_JOB_H
#define XT_NODE_FS_ASYNC_JOB_H

#include "../node_common.h"
#include "fs_common.h"

typedef enum {
  XT_FS_JOB_READ_FILE,
  XT_FS_JOB_WRITE_FILE,
  XT_FS_JOB_MKDIR,
  XT_FS_JOB_READDIR,
  XT_FS_JOB_RM,
  XT_FS_JOB_UNLINK,
  XT_FS_JOB_RMDIR,
  XT_FS_JOB_RENAME,
  XT_FS_JOB_COPY_FILE,
  XT_FS_JOB_CP,
  XT_FS_JOB_REALPATH,
  XT_FS_JOB_STAT,
  XT_FS_JOB_LSTAT,
  XT_FS_JOB_STATFS,
  XT_FS_JOB_ACCESS,
  XT_FS_JOB_OPEN,
  XT_FS_JOB_CHMOD,
  XT_FS_JOB_LCHMOD,
  XT_FS_JOB_CHOWN,
  XT_FS_JOB_LCHOWN,
  XT_FS_JOB_TRUNCATE,
  XT_FS_JOB_UTIMES,
  XT_FS_JOB_LUTIMES,
  XT_FS_JOB_LINK,
  XT_FS_JOB_SYMLINK,
  XT_FS_JOB_READLINK,
  XT_FS_JOB_MKDTEMP,
  XT_FS_JOB_OPENDIR,
  XT_FS_JOB_GLOB,
  XT_FS_JOB_FD_READ,
  XT_FS_JOB_FD_WRITE,
  XT_FS_JOB_FD_READ_FILE,
  XT_FS_JOB_FD_WRITE_FILE,
  XT_FS_JOB_FD_CLOSE,
  XT_FS_JOB_FD_STAT,
  XT_FS_JOB_FD_TRUNCATE,
  XT_FS_JOB_FD_CHMOD,
  XT_FS_JOB_FD_CHOWN,
  XT_FS_JOB_FD_UTIMES,
  XT_FS_JOB_FD_SYNC,
  XT_FS_JOB_FD_DATASYNC,
} xt_fs_job_kind;

typedef struct {
  xt_async_job base;
  xt_fs_job_kind kind;

  /* input */
  char *path;
  char *path2;
  char *text; /* encoding, symlink type, or a single glob pattern */
  char **patterns;
  size_t pattern_count;
  unsigned char *data;
  size_t data_length;
  int flags;
  int mode;
  int fd;
  long long offset;
  long long length;
  long long position;
  int uid;
  int gid;
  double atime;
  double mtime;
  int recursive;
  int force;
  int error_on_exist;
  int dereference;
  int preserve;
  int with_types;

  /* worker results */
  int err;
  const char *syscall;
  char *error_path;
  xt_fs_stat_t stat;
  unsigned char *bytes;
  size_t bytes_length;
  char *string;
  xt_fs_entry *entries;
  size_t entry_count;
  long long numbers[7];
  int result_fd;
  long long written;
} xt_fs_job;

/* Value builders shared with the synchronous implementation (fs_ops.c). */
xt_value xt_node_stat_result(const xt_fs_stat_t *info);
xt_value xt_node_dirent_named(const char *name, unsigned int mode);
xt_value xt_node_dir_from_entries(const char *path, xt_fs_entry *entries, size_t count);

/* FileHandle factory (async_fh.c). */
xt_value xt_fh_new(xt_value fdValue);

/* Job lifecycle (async_job.c). */
xt_fs_job *xt_fs_job_new(xt_fs_job_kind kind);
/** Create the promise and submit; returns it. */
xt_value xt_fs_job_submit(xt_fs_job *job);
void xt_fs_job_fail(xt_fs_job *job, int err, const char *syscall, const char *path);

/* Implemented by async_worker.c / async_complete.c. */
void xt_fs_job_run(xt_async_job *job);
void xt_fs_job_complete(xt_async_job *job);
void xt_fs_job_release(xt_async_job *job);

/* Argument helpers (async_job.c). */
char *xt_fs_copy_cstr(const char *text);
char *xt_fs_take_string(xt_value value);

#endif /* XT_NODE_FS_ASYNC_JOB_H */
