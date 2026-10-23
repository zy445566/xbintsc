/*
 * Worker half of `fs/promises`: the blocking syscalls.
 *
 * Everything in this file runs on an async I/O worker thread. It may only touch
 * plain C memory — no `xt_value`, no collector, no exceptions. Results are
 * stored on the job and interpreted by async_complete.c on the main thread.
 */

#include "async_job.h"
#include "async_worker_dir.h"

#include <errno.h>
#include <stdio.h>
#include <time.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/mount.h>
#elif defined(__linux__)
#include <sys/vfs.h>
#endif

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#define xt_fs_fstat_fn xt_fs_win_fstat
#else
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#define xt_fs_fstat_fn fstat
#endif

/* -- small file helpers ---------------------------------------------------- */

int xt_fs_worker_read_fd(int fd, unsigned char **out, size_t *out_length) {
  size_t capacity = 8192;
  size_t length = 0;
  unsigned char *bytes = (unsigned char *)malloc(capacity);
  if (!bytes) return -1;
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      unsigned char *grown = (unsigned char *)realloc(bytes, capacity);
      if (!grown) {
        free(bytes);
        return -1;
      }
      bytes = grown;
    }
    ssize_t count = XT_FS_READ(fd, bytes + length, capacity - length);
    if (count < 0) {
      int saved = errno;
      free(bytes);
      errno = saved;
      return -1;
    }
    if (count == 0) break;
    length += (size_t)count;
  }
  *out = bytes;
  *out_length = length;
  return 0;
}

int xt_fs_worker_read_file(const char *path, unsigned char **out, size_t *out_length) {
  int fd = XT_FS_OPEN(path, XT_FS_RDONLY | XT_FS_BINARY_FLAG, 0);
  if (fd < 0) return -1;
  int status = xt_fs_worker_read_fd(fd, out, out_length);
  int saved = errno;
  XT_FS_CLOSE(fd);
  errno = saved;
  return status;
}

int xt_fs_worker_write_fd(int fd, const unsigned char *data, size_t length) {
  size_t written = 0;
  while (written < length) {
    ssize_t count = XT_FS_WRITE(fd, data + written, length - written);
    if (count <= 0) return -1;
    written += (size_t)count;
  }
  return 0;
}

int xt_fs_worker_write_file(const char *path, const unsigned char *data, size_t length, int append) {
  int flags = XT_FS_WRONLY | XT_FS_CREAT | XT_FS_BINARY_FLAG | (append ? XT_FS_APPEND : XT_FS_TRUNC);
  int fd = XT_FS_OPEN(path, flags, 0666);
  if (fd < 0) return -1;
  int status = xt_fs_worker_write_fd(fd, data, length);
  int saved = errno;
  if (XT_FS_CLOSE(fd) != 0 && status == 0) {
    status = -1;
    saved = errno;
  }
  errno = saved;
  return status;
}

int xt_fs_entry_push(xt_fs_job *job, const char *name, const char *bare, unsigned int mode) {
  xt_fs_entry *grown = (xt_fs_entry *)realloc(job->entries, sizeof(xt_fs_entry) * (job->entry_count + 1));
  if (!grown) return -1;
  job->entries = grown;
  xt_fs_entry *entry = &job->entries[job->entry_count];
  entry->name = xt_fs_copy_cstr(name);
  entry->bare = bare && strcmp(bare, name) != 0 ? xt_fs_copy_cstr(bare) : entry->name;
  entry->mode = mode;
  if (!entry->name || !entry->bare) return -1;
  job->entry_count++;
  return 0;
}

char *xt_fs_worker_join(const char *dir, const char *name) {
  size_t dirLength = strlen(dir);
  int needsSlash = dirLength > 0 && dir[dirLength - 1] != '/' && dir[dirLength - 1] != '\\';
  size_t size = dirLength + (needsSlash ? 1 : 0) + strlen(name) + 1;
  char *path = (char *)malloc(size);
  if (!path) return NULL;
  snprintf(path, size, "%s%s%s", dir, needsSlash ? "/" : "", name);
  return path;
}

/* -- path operations ------------------------------------------------------- */

static void xt_fs_worker_mkdir(xt_fs_job *job) {
  if (!job->path) {
    xt_fs_job_fail(job, EINVAL, "mkdir", NULL);
    return;
  }
  if (!job->recursive) {
    if (xt_fs_mkdir_mode(job->path, job->mode) != 0 && errno != EEXIST) {
      xt_fs_job_fail(job, errno, "mkdir", job->path);
    }
    return;
  }
  if (xt_fs_mkdir_parents(job->path, job->mode) != 0) {
    xt_fs_job_fail(job, errno, "mkdir", job->path);
  }
}

static void xt_fs_worker_realpath(xt_fs_job *job) {
  if (!job->path) {
    xt_fs_job_fail(job, EINVAL, "resolve", NULL);
    return;
  }
#if defined(_WIN32)
  char *resolved = _fullpath(NULL, job->path, 0);
#else
  char *resolved = realpath(job->path, NULL);
#endif
  if (!resolved) {
    xt_fs_job_fail(job, errno, "resolve", job->path);
    return;
  }
  job->string = resolved;
}

static void xt_fs_worker_statfs(xt_fs_job *job) {
#if defined(_WIN32) || defined(_AIX) || defined(__sun)
  (void)job;
  for (int i = 0; i < 7; i++) job->numbers[i] = 0;
#else
  if (!job->path) {
    xt_fs_job_fail(job, EINVAL, "statfs", NULL);
    return;
  }
  struct statfs info;
  if (statfs(job->path, &info) != 0) {
    xt_fs_job_fail(job, errno, "statfs", job->path);
    return;
  }
  job->numbers[0] = (long long)info.f_type;
  job->numbers[1] = (long long)info.f_bsize;
  job->numbers[2] = (long long)info.f_blocks;
  job->numbers[3] = (long long)info.f_bfree;
  job->numbers[4] = (long long)info.f_bavail;
  job->numbers[5] = (long long)info.f_files;
  job->numbers[6] = (long long)info.f_ffree;
#endif
}

static void xt_fs_worker_mkdtemp(xt_fs_job *job) {
  if (!job->path) {
    xt_fs_job_fail(job, EINVAL, "mkdtemp", NULL);
    return;
  }
  size_t length = strlen(job->path);
  char *dir = (char *)malloc(length + 7);
  if (!dir) {
    xt_fs_job_fail(job, ENOMEM, "mkdtemp", job->path);
    return;
  }
  memcpy(dir, job->path, length);
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  /* Concurrent mkdtemp jobs must not walk the same candidate sequence; mix in
   * this job's allocation address, which differs per thread. */
  unsigned int noise = (unsigned int)(uintptr_t)dir;
  for (int attempt = 0; attempt < 100; attempt++) {
    unsigned int seed = (unsigned int)time(NULL) ^ noise ^ (unsigned int)(attempt * 2654435761u);
    for (int i = 0; i < 6; i++) {
      dir[length + (size_t)i] = alphabet[seed % 36u];
      seed = seed * 1103515245u + 12345u;
    }
    dir[length + 6] = '\0';
    if (xt_fs_mkdir_one(dir) == 0) {
      job->string = dir;
      return;
    }
    if (errno != EEXIST) break;
  }
  int saved = errno;
  free(dir);
  xt_fs_job_fail(job, saved, "mkdtemp", job->path);
}

#if defined(_WIN32)
static void xt_fs_worker_filetime(double seconds, FILETIME *out) {
  long long ticks = (long long)(seconds * 10000000.0) + 116444736000000000LL;
  unsigned long long value = (unsigned long long)ticks;
  out->dwLowDateTime = (DWORD)value;
  out->dwHighDateTime = (DWORD)(value >> 32);
}

static int xt_fs_worker_set_handle_times(HANDLE handle, double atime, double mtime) {
  FILETIME accessTime;
  FILETIME modifyTime;
  FILETIME *accessPtr = NULL;
  FILETIME *modifyPtr = NULL;
  if (atime == atime) {
    xt_fs_worker_filetime(atime, &accessTime);
    accessPtr = &accessTime;
  }
  if (mtime == mtime) {
    xt_fs_worker_filetime(mtime, &modifyTime);
    modifyPtr = &modifyTime;
  }
  return SetFileTime(handle, NULL, accessPtr, modifyPtr) ? 0 : -1;
}

static int xt_fs_worker_set_path_times(const char *path, double atime, double mtime) {
  HANDLE handle = CreateFileA(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (handle == INVALID_HANDLE_VALUE) return -1;
  int status = xt_fs_worker_set_handle_times(handle, atime, mtime);
  int saved = errno;
  CloseHandle(handle);
  errno = saved;
  return status;
}
#endif

static void xt_fs_worker_utimes(xt_fs_job *job, int follow) {
  if (!job->path) {
    xt_fs_job_fail(job, EINVAL, "utime", NULL);
    return;
  }
#if defined(_WIN32)
  (void)follow;
  if (xt_fs_worker_set_path_times(job->path, job->atime, job->mtime) != 0) {
    xt_fs_job_fail(job, errno ? errno : EPERM, "utime", job->path);
  }
#else
  struct timeval times[2];
  times[0].tv_sec = (time_t)job->atime;
  times[0].tv_usec = (suseconds_t)((job->atime - (double)times[0].tv_sec) * 1000000.0);
  times[1].tv_sec = (time_t)job->mtime;
  times[1].tv_usec = (suseconds_t)((job->mtime - (double)times[1].tv_sec) * 1000000.0);
#if defined(__linux__)
  int status = follow ? utimes(job->path, times) : lutimes(job->path, times);
#else
  (void)follow;
  int status = utimes(job->path, times);
#endif
  if (status != 0) xt_fs_job_fail(job, errno, "utime", job->path);
#endif
}

/* -- FileHandle operations ------------------------------------------------- */

static void xt_fs_worker_fd_utimes(xt_fs_job *job) {
#if defined(_WIN32)
  intptr_t osHandle = _get_osfhandle(job->fd);
  if (osHandle == -1 || xt_fs_worker_set_handle_times((HANDLE)osHandle, job->atime, job->mtime) != 0) {
    xt_fs_job_fail(job, errno ? errno : EPERM, "futime", NULL);
  }
#else
  struct timeval times[2];
  times[0].tv_sec = (time_t)job->atime;
  times[0].tv_usec = (suseconds_t)((job->atime - (double)times[0].tv_sec) * 1000000.0);
  times[1].tv_sec = (time_t)job->mtime;
  times[1].tv_usec = (suseconds_t)((job->mtime - (double)times[1].tv_sec) * 1000000.0);
  if (futimes(job->fd, times) != 0) xt_fs_job_fail(job, errno, "futime", NULL);
#endif
}

/* -- dispatch -------------------------------------------------------------- */

void xt_fs_job_run(xt_async_job *base) {
  xt_fs_job *job = (xt_fs_job *)base;
  switch (job->kind) {
    case XT_FS_JOB_READ_FILE:
      if (!job->path) {
        xt_fs_job_fail(job, EINVAL, "open", NULL);
        break;
      }
      if (xt_fs_worker_read_file(job->path, &job->bytes, &job->bytes_length) != 0) {
        xt_fs_job_fail(job, errno, "open", job->path);
      }
      break;
    case XT_FS_JOB_WRITE_FILE:
      if (!job->path) {
        xt_fs_job_fail(job, EINVAL, "open", NULL);
        break;
      }
      if (xt_fs_worker_write_file(job->path, job->data, job->data_length, job->flags != 0) != 0) {
        xt_fs_job_fail(job, errno, "open", job->path);
      }
      break;
    case XT_FS_JOB_MKDIR: xt_fs_worker_mkdir(job); break;
    case XT_FS_JOB_READDIR: xt_fs_worker_readdir(job); break;
    case XT_FS_JOB_RM: xt_fs_worker_remove_tree(job); break;
    case XT_FS_JOB_UNLINK:
      if (!job->path) {
        xt_fs_job_fail(job, EINVAL, "unlink", NULL);
        break;
      }
#if defined(_WIN32)
      if (_unlink(job->path) != 0) xt_fs_job_fail(job, errno, "unlink", job->path);
#else
      if (unlink(job->path) != 0) xt_fs_job_fail(job, errno, "unlink", job->path);
#endif
      break;
    case XT_FS_JOB_RMDIR:
      if (!job->path || xt_fs_rmdir_one(job->path) != 0) xt_fs_job_fail(job, errno, "rmdir", job->path);
      break;
    case XT_FS_JOB_RENAME:
      if (!job->path || !job->path2 || rename(job->path, job->path2) != 0) {
        xt_fs_job_fail(job, errno, "rename", job->path);
      }
      break;
    case XT_FS_JOB_COPY_FILE: xt_fs_worker_copy_file(job); break;
    case XT_FS_JOB_CP: xt_fs_worker_copy_tree(job); break;
    case XT_FS_JOB_REALPATH: xt_fs_worker_realpath(job); break;
    case XT_FS_JOB_STAT:
    case XT_FS_JOB_LSTAT:
      if (!job->path) {
        xt_fs_job_fail(job, EINVAL, "stat", NULL);
        break;
      }
      if ((job->kind == XT_FS_JOB_STAT ? xt_fs_stat_fn(job->path, &job->stat)
                                       : xt_fs_lstat_fn(job->path, &job->stat)) != 0) {
        xt_fs_job_fail(job, errno, "stat", job->path);
      }
      break;
    case XT_FS_JOB_STATFS: xt_fs_worker_statfs(job); break;
    case XT_FS_JOB_ACCESS:
      if (!job->path || xt_fs_access(job->path, job->mode) != 0) xt_fs_job_fail(job, errno, "access", job->path);
      break;
    case XT_FS_JOB_OPEN:
      if (!job->path) {
        xt_fs_job_fail(job, EINVAL, "open", NULL);
        break;
      }
#if defined(_WIN32)
      job->result_fd = _open(job->path, job->flags, job->mode);
#else
      job->result_fd = open(job->path, job->flags, (mode_t)job->mode);
#endif
      if (job->result_fd < 0) xt_fs_job_fail(job, errno, "open", job->path);
      break;
    case XT_FS_JOB_CHMOD:
    case XT_FS_JOB_LCHMOD:
#if defined(_WIN32)
      if (job->kind == XT_FS_JOB_LCHMOD) break;
      if (!job->path || _chmod(job->path, job->mode) != 0) xt_fs_job_fail(job, errno, "chmod", job->path);
#else
      if (!job->path || chmod(job->path, (mode_t)job->mode) != 0) xt_fs_job_fail(job, errno, "chmod", job->path);
#endif
      break;
    case XT_FS_JOB_CHOWN:
    case XT_FS_JOB_LCHOWN:
#if defined(_WIN32)
      break;
#else
      if (!job->path ||
          (job->kind == XT_FS_JOB_CHOWN ? chown(job->path, (uid_t)job->uid, (gid_t)job->gid)
                                        : lchown(job->path, (uid_t)job->uid, (gid_t)job->gid)) != 0) {
        xt_fs_job_fail(job, errno, "chown", job->path);
      }
      break;
#endif
    case XT_FS_JOB_TRUNCATE:
      if (!job->path) {
        xt_fs_job_fail(job, EINVAL, "truncate", NULL);
        break;
      }
#if defined(_WIN32)
      {
        int fd = _open(job->path, _O_RDWR | _O_BINARY);
        int status = fd < 0 ? -1 : _chsize_s(fd, job->length);
        int saved = errno;
        if (fd >= 0) _close(fd);
        if (status != 0) xt_fs_job_fail(job, saved, "truncate", job->path);
      }
#else
      if (truncate(job->path, (off_t)job->length) != 0) xt_fs_job_fail(job, errno, "truncate", job->path);
#endif
      break;
    case XT_FS_JOB_UTIMES: xt_fs_worker_utimes(job, 1); break;
    case XT_FS_JOB_LUTIMES: xt_fs_worker_utimes(job, 0); break;
    case XT_FS_JOB_LINK:
#if defined(_WIN32)
      if (!job->path || !job->path2 || !CreateHardLinkA(job->path2, job->path, NULL)) {
        xt_fs_job_fail(job, errno ? errno : EPERM, "link", job->path2);
      }
#else
      if (!job->path || !job->path2 || link(job->path, job->path2) != 0) {
        xt_fs_job_fail(job, errno, "link", job->path2);
      }
#endif
      break;
    case XT_FS_JOB_SYMLINK:
#if defined(_WIN32)
      {
        DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        if (job->text && (strcmp(job->text, "dir") == 0 || strcmp(job->text, "junction") == 0)) {
          flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
        }
        if (!job->path || !job->path2 || !CreateSymbolicLinkA(job->path2, job->path, flags)) {
          xt_fs_job_fail(job, errno ? errno : EPERM, "symlink", job->path2);
        }
      }
#else
      if (!job->path || !job->path2 || symlink(job->path, job->path2) != 0) {
        xt_fs_job_fail(job, errno, "symlink", job->path2);
      }
#endif
      break;
    case XT_FS_JOB_READLINK:
#if defined(_WIN32)
      xt_fs_job_fail(job, ENOSYS, "readlink", job->path);
#else
      if (!job->path) {
        xt_fs_job_fail(job, ENOENT, "readlink", NULL);
        break;
      }
      {
        size_t capacity = 256;
        for (;;) {
          char *buffer = (char *)malloc(capacity);
          if (!buffer) {
            xt_fs_job_fail(job, ENOMEM, "readlink", job->path);
            break;
          }
          ssize_t length = readlink(job->path, buffer, capacity);
          if (length < 0) {
            int saved = errno;
            free(buffer);
            xt_fs_job_fail(job, saved, "readlink", job->path);
            break;
          }
          if ((size_t)length < capacity) {
            buffer[length] = '\0';
            job->string = buffer;
            break;
          }
          free(buffer);
          capacity *= 2;
        }
      }
#endif
      break;
    case XT_FS_JOB_MKDTEMP: xt_fs_worker_mkdtemp(job); break;
    case XT_FS_JOB_OPENDIR: xt_fs_worker_opendir(job); break;
    case XT_FS_JOB_GLOB: xt_fs_worker_glob(job); break;
    case XT_FS_JOB_FD_READ: xt_fs_worker_fd_read(job); break;
    case XT_FS_JOB_FD_WRITE: xt_fs_worker_fd_write(job); break;
    case XT_FS_JOB_FD_READ_FILE:
      if (xt_fs_worker_read_fd(job->fd, &job->bytes, &job->bytes_length) != 0) {
        xt_fs_job_fail(job, errno, "read", NULL);
      }
      break;
    case XT_FS_JOB_FD_WRITE_FILE:
      if (xt_fs_worker_write_fd(job->fd, job->data, job->data_length) != 0) {
        xt_fs_job_fail(job, errno, "write", NULL);
      }
      break;
    case XT_FS_JOB_FD_CLOSE:
      if (XT_FS_CLOSE(job->fd) != 0) xt_fs_job_fail(job, errno, "close", NULL);
      break;
    case XT_FS_JOB_FD_STAT:
      if (xt_fs_fstat_fn(job->fd, &job->stat) != 0) xt_fs_job_fail(job, errno, "fstat", NULL);
      break;
    case XT_FS_JOB_FD_TRUNCATE:
#if defined(_WIN32)
      if (_chsize_s(job->fd, job->length) != 0) xt_fs_job_fail(job, errno, "ftruncate", NULL);
#else
      if (ftruncate(job->fd, (off_t)job->length) != 0) xt_fs_job_fail(job, errno, "ftruncate", NULL);
#endif
      break;
    case XT_FS_JOB_FD_CHMOD:
#if defined(_WIN32)
      break;
#else
      if (fchmod(job->fd, (mode_t)job->mode) != 0) xt_fs_job_fail(job, errno, "fchmod", NULL);
#endif
      break;
    case XT_FS_JOB_FD_CHOWN:
#if defined(_WIN32)
      break;
#else
      if (fchown(job->fd, (uid_t)job->uid, (gid_t)job->gid) != 0) xt_fs_job_fail(job, errno, "fchown", NULL);
#endif
      break;
    case XT_FS_JOB_FD_UTIMES: xt_fs_worker_fd_utimes(job); break;
    case XT_FS_JOB_FD_SYNC:
#if defined(_WIN32)
      if (_commit(job->fd) != 0) xt_fs_job_fail(job, errno, "fsync", NULL);
#else
      if (fsync(job->fd) != 0) xt_fs_job_fail(job, errno, "fsync", NULL);
#endif
      break;
    case XT_FS_JOB_FD_DATASYNC:
#if defined(_WIN32)
      if (_commit(job->fd) != 0) xt_fs_job_fail(job, errno, "fdatasync", NULL);
#else
#if defined(__APPLE__)
      if (fsync(job->fd) != 0) xt_fs_job_fail(job, errno, "fdatasync", NULL);
#else
      if (fdatasync(job->fd) != 0) xt_fs_job_fail(job, errno, "fdatasync", NULL);
#endif
#endif
      break;
  }
}
