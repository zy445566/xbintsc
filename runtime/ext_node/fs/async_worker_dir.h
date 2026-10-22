/*
 * Directory-walking jobs shared by the worker translation units.
 *
 * These run on an async worker thread and only touch plain C memory. Their
 * results are `xt_fs_entry` arrays on the job, turned into strings, `Dirent`s or
 * `Dir` objects by async_complete.c.
 */
#ifndef XT_NODE_FS_ASYNC_WORKER_DIR_H
#define XT_NODE_FS_ASYNC_WORKER_DIR_H

#include "async_job.h"

void xt_fs_worker_readdir(xt_fs_job *job);
void xt_fs_worker_opendir(xt_fs_job *job);
void xt_fs_worker_remove_tree(xt_fs_job *job);
void xt_fs_worker_copy_tree(xt_fs_job *job);
void xt_fs_worker_glob(xt_fs_job *job);

/* Shared byte helpers (async_worker.c). */
int xt_fs_worker_read_fd(int fd, unsigned char **out, size_t *out_length);
int xt_fs_worker_read_file(const char *path, unsigned char **out, size_t *out_length);
int xt_fs_worker_write_fd(int fd, const unsigned char *data, size_t length);
int xt_fs_worker_write_file(const char *path, const unsigned char *data, size_t length, int append);
int xt_fs_entry_push(xt_fs_job *job, const char *name, const char *bare, unsigned int mode);
char *xt_fs_worker_join(const char *dir, const char *name);
int xt_fs_worker_copy_bytes(const char *from, const char *to);
void xt_fs_worker_copy_file(xt_fs_job *job);

/* Descriptor read/write jobs (async_worker_fd.c). */
void xt_fs_worker_fd_read(xt_fs_job *job);
void xt_fs_worker_fd_write(xt_fs_job *job);

#endif /* XT_NODE_FS_ASYNC_WORKER_DIR_H */
