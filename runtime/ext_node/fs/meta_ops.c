/*
 * Node.js `fs` metadata / temporary-directory operations for xbintsc.
 *
 *     accessSync, chmodSync, chownSync, truncateSync, utimesSync, lutimesSync,
 *     statfsSync, mkdtempSync
 *
 * Errors throw Node-shaped `Error` objects (see `fs_common.h`).
 */

#include "../node_common.h"
#include "fs_common.h"

#include <sys/stat.h>
#include <sys/types.h>
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/mount.h>
#elif defined(__linux__)
#include <sys/vfs.h>
#endif

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#define xt_fs_chmod_fn _chmod
#define xt_fs_truncate_path(path, length) xt_fs_win_truncate(path, length)
#else
#include <sys/time.h>
#include <unistd.h>
#define xt_fs_chmod_fn chmod
#define xt_fs_truncate_path(path, length) truncate((path), (off_t)(length))
#endif

#if defined(_WIN32)
static int xt_fs_errno_from_win32(DWORD error) {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_BAD_NETPATH:
    case ERROR_INVALID_DRIVE:
      return ENOENT;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
      return EACCES;
    case ERROR_DIRECTORY:
      return ENOTDIR;
#ifdef ENAMETOOLONG
    case ERROR_FILENAME_EXCED_RANGE:
      return ENAMETOOLONG;
#endif
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
      return ENOMEM;
    default:
      return EINVAL;
  }
}

/* FILETIME (100 ns ticks since 1601-01-01 UTC) -> Unix seconds, in UTC.
 * Unlike the CRT's `_stat` this never round-trips through the local time zone,
 * so epoch-relative times stay representable regardless of the machine's
 * timezone (matching libuv/Node). */
static long long xt_fs_filetime_to_seconds(const FILETIME *time) {
  unsigned long long value = ((unsigned long long)time->dwHighDateTime << 32) | time->dwLowDateTime;
  if (value == 0) return 0;
  return ((long long)value - 116444736000000000LL) / 10000000LL;
}

static void xt_fs_win_fill_stat(xt_fs_stat_t *out, DWORD attributes, DWORD volume,
                                unsigned long long index, unsigned int links, long long size,
                                const FILETIME *creation, const FILETIME *access,
                                const FILETIME *write) {
  memset(out, 0, sizeof(*out));
  if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
    out->st_mode = _S_IFDIR;
    size = 0;
  } else {
    out->st_mode = _S_IFREG;
  }
  if (attributes & FILE_ATTRIBUTE_READONLY) {
    out->st_mode |= _S_IREAD | (_S_IREAD >> 3) | (_S_IREAD >> 6);
  } else {
    out->st_mode |= (_S_IREAD | _S_IWRITE) | ((_S_IREAD | _S_IWRITE) >> 3) |
                    ((_S_IREAD | _S_IWRITE) >> 6);
  }
  out->st_size = size;
  out->st_dev = volume;
  out->st_ino = index;
  out->st_nlink = links ? links : 1;
  out->st_ctime = xt_fs_filetime_to_seconds(creation);
  out->st_atime = xt_fs_filetime_to_seconds(access);
  out->st_mtime = xt_fs_filetime_to_seconds(write);
}

int xt_fs_win_stat(const char *path, xt_fs_stat_t *out) {
  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!path || !GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
    errno = xt_fs_errno_from_win32(GetLastError());
    return -1;
  }
  long long size = ((long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
  xt_fs_win_fill_stat(out, data.dwFileAttributes, 0, 0, 1, size, &data.ftCreationTime,
                      &data.ftLastAccessTime, &data.ftLastWriteTime);
  /* Best effort: fill the volume serial, file index and link count like Node. */
  HANDLE handle = CreateFileA(path, FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (handle != INVALID_HANDLE_VALUE) {
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileInformationByHandle(handle, &info)) {
      out->st_dev = info.dwVolumeSerialNumber;
      out->st_ino = ((unsigned long long)info.nFileIndexHigh << 32) | info.nFileIndexLow;
      out->st_nlink = info.nNumberOfLinks ? info.nNumberOfLinks : 1;
    }
    CloseHandle(handle);
  }
  return 0;
}

int xt_fs_win_lstat(const char *path, xt_fs_stat_t *out) { return xt_fs_win_stat(path, out); }

int xt_fs_win_fstat(int fd, xt_fs_stat_t *out) {
  intptr_t os_handle = _get_osfhandle(fd);
  if (os_handle == -1) {
    errno = EBADF;
    return -1;
  }
  BY_HANDLE_FILE_INFORMATION info;
  if (GetFileInformationByHandle((HANDLE)os_handle, &info)) {
    long long size = ((long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    xt_fs_win_fill_stat(out, info.dwFileAttributes, info.dwVolumeSerialNumber,
                        ((unsigned long long)info.nFileIndexHigh << 32) | info.nFileIndexLow,
                        info.nNumberOfLinks, size, &info.ftCreationTime, &info.ftLastAccessTime,
                        &info.ftLastWriteTime);
    return 0;
  }
  DWORD type = GetFileType((HANDLE)os_handle);
  if (type == FILE_TYPE_CHAR || type == FILE_TYPE_PIPE) {
    memset(out, 0, sizeof(*out));
    out->st_mode = (type == FILE_TYPE_PIPE ? _S_IFIFO : _S_IFCHR) | _S_IREAD | _S_IWRITE |
                   (_S_IREAD >> 3) | (_S_IWRITE >> 3) | (_S_IREAD >> 6) | (_S_IWRITE >> 6);
    out->st_nlink = 1;
    out->st_ino = (unsigned long long)(uintptr_t)os_handle;
    return 0;
  }
  errno = xt_fs_errno_from_win32(GetLastError());
  return -1;
}

static int xt_fs_win_truncate(const char *path, long long length) {
  int fd = _open(path, _O_RDWR | _O_BINARY);
  if (fd < 0) return -1;
  int result = _chsize_s(fd, length);
  _close(fd);
  return result;
}

/* Unix seconds -> Windows FILETIME (100 ns ticks since 1601-01-01 UTC). The CRT
 * `_utime`/`_utimbuf` path goes through local time, which is lossy around DST
 * boundaries; matching libuv's direct conversion keeps the result exact and
 * identical to Node. */
static void xt_fs_unix_to_filetime(double seconds, FILETIME *out) {
  long long ticks = (long long)(seconds * 10000000.0) + 116444736000000000LL;
  unsigned long long value = (unsigned long long)ticks;
  out->dwLowDateTime = (DWORD)value;
  out->dwHighDateTime = (DWORD)(value >> 32);
}

static int xt_fs_set_file_times(HANDLE handle, double atime, double mtime) {
  FILETIME access_time;
  FILETIME modify_time;
  FILETIME *access_ptr = NULL;
  FILETIME *modify_ptr = NULL;
  if (atime == atime) {
    xt_fs_unix_to_filetime(atime, &access_time);
    access_ptr = &access_time;
  }
  if (mtime == mtime) {
    xt_fs_unix_to_filetime(mtime, &modify_time);
    modify_ptr = &modify_time;
  }
  return SetFileTime(handle, NULL, access_ptr, modify_ptr) ? 0 : -1;
}
#endif

/* ------------------------------------------------------------------------- */
/* accessSync                                                                */
/* ------------------------------------------------------------------------- */

xt_value xt_node_access(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_node_cstr(argv[0]);
  int mode = argc > 1 ? (int)xt_to_number(argv[1]) : F_OK;
  if (!path || xt_fs_access(path, mode) != 0) {
    xt_fs_raise_path("access", path);
    return xt_undefined();
  }
  return xt_undefined();
}

/* ------------------------------------------------------------------------- */
/* chmodSync / chownSync                                                     */
/* ------------------------------------------------------------------------- */

xt_value xt_node_chmod(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_undefined();
  const char *path = xt_node_cstr(argv[0]);
  int mode = (int)xt_to_number(argv[1]);
  if (!path || xt_fs_chmod_fn(path, mode) != 0) {
    xt_fs_raise_path("chmod", path);
    return xt_undefined();
  }
  return xt_undefined();
}

xt_value xt_node_chown(int32_t argc, xt_value *argv) {
  if (argc < 3) return xt_undefined();
  const char *path = xt_node_cstr(argv[0]);
  int uid = (int)xt_to_number(argv[1]);
  int gid = (int)xt_to_number(argv[2]);
#if defined(_WIN32)
  (void)path;
  (void)uid;
  (void)gid;
  return xt_undefined();
#else
  if (!path || chown(path, (uid_t)uid, (gid_t)gid) != 0) {
    xt_fs_raise_path("chown", path);
    return xt_undefined();
  }
  return xt_undefined();
#endif
}

xt_value xt_node_lchown(int32_t argc, xt_value *argv) {
  if (argc < 3) return xt_undefined();
  const char *path = xt_node_cstr(argv[0]);
  int uid = (int)xt_to_number(argv[1]);
  int gid = (int)xt_to_number(argv[2]);
#if defined(_WIN32)
  (void)path;
  (void)uid;
  (void)gid;
  return xt_undefined();
#else
  if (!path || lchown(path, (uid_t)uid, (gid_t)gid) != 0) {
    xt_fs_raise_path("lchown", path);
    return xt_undefined();
  }
  return xt_undefined();
#endif
}

/* Windows has no `lchmod`; Node ignores the call there. */
xt_value xt_node_lchmod(int32_t argc, xt_value *argv) {
#if defined(_WIN32)
  (void)argc;
  (void)argv;
  return xt_undefined();
#else
  return xt_node_chmod(argc, argv);
#endif
}

/* ------------------------------------------------------------------------- */
/* truncateSync                                                              */
/* ------------------------------------------------------------------------- */

xt_value xt_node_truncate(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_node_cstr(argv[0]);
  long long length = argc > 1 ? (long long)xt_to_number(argv[1]) : 0;
  if (!path || xt_fs_truncate_path(path, length) != 0) {
    xt_fs_raise_path("truncate", path);
    return xt_undefined();
  }
  return xt_undefined();
}

/* ------------------------------------------------------------------------- */
/* utimesSync / lutimesSync                                                  */
/* ------------------------------------------------------------------------- */

xt_value xt_node_utimes(int32_t argc, xt_value *argv) {
  if (argc < 3) return xt_undefined();
  const char *path = xt_node_cstr(argv[0]);
  double atime = xt_fs_time_seconds(argv[1]);
  double mtime = xt_fs_time_seconds(argv[2]);
#if defined(_WIN32)
  HANDLE handle = path ? CreateFileA(path, FILE_WRITE_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                     OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL)
                        : INVALID_HANDLE_VALUE;
  if (handle == INVALID_HANDLE_VALUE || xt_fs_set_file_times(handle, atime, mtime) != 0) {
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    xt_fs_raise_path("utime", path);
    return xt_undefined();
  }
  CloseHandle(handle);
  return xt_undefined();
#else
  struct timeval times[2];
  times[0].tv_sec = (time_t)atime;
  times[0].tv_usec = (suseconds_t)((atime - (double)times[0].tv_sec) * 1000000.0);
  times[1].tv_sec = (time_t)mtime;
  times[1].tv_usec = (suseconds_t)((mtime - (double)times[1].tv_sec) * 1000000.0);
  if (!path || utimes(path, times) != 0) {
    xt_fs_raise_path("utimes", path);
    return xt_undefined();
  }
  return xt_undefined();
#endif
}

xt_value xt_node_lutimes(int32_t argc, xt_value *argv) {
#if defined(__linux__)
  if (argc < 3) return xt_undefined();
  const char *path = xt_node_cstr(argv[0]);
  double atime = xt_fs_time_seconds(argv[1]);
  double mtime = xt_fs_time_seconds(argv[2]);
  struct timeval times[2];
  times[0].tv_sec = (time_t)atime;
  times[0].tv_usec = (suseconds_t)((atime - (double)times[0].tv_sec) * 1000000.0);
  times[1].tv_sec = (time_t)mtime;
  times[1].tv_usec = (suseconds_t)((mtime - (double)times[1].tv_sec) * 1000000.0);
  if (!path || lutimes(path, times) != 0) {
    xt_fs_raise_path("lutimes", path);
    return xt_undefined();
  }
  return xt_undefined();
#else
  /* macOS and Windows have no `lutimes`; fall back to `utimes`. */
  return xt_node_utimes(argc, argv);
#endif
}

/* ------------------------------------------------------------------------- */
/* statfsSync                                                                */
/* ------------------------------------------------------------------------- */

xt_value xt_node_statfs(int32_t argc, xt_value *argv) {
  xt_value object = xt_object_new();
#if defined(_WIN32) || defined(_AIX) || defined(__sun)
  (void)argc;
  (void)argv;
  xt_object_set(object, xt_string_from_cstr("type"), xt_number(0));
  xt_object_set(object, xt_string_from_cstr("bsize"), xt_number(0));
  xt_object_set(object, xt_string_from_cstr("blocks"), xt_number(0));
  xt_object_set(object, xt_string_from_cstr("bfree"), xt_number(0));
  xt_object_set(object, xt_string_from_cstr("bavail"), xt_number(0));
  xt_object_set(object, xt_string_from_cstr("files"), xt_number(0));
  xt_object_set(object, xt_string_from_cstr("ffree"), xt_number(0));
#else
  const char *path = argc > 0 ? xt_node_cstr(argv[0]) : NULL;
  struct statfs info;
  if (!path || statfs(path, &info) != 0) {
    xt_fs_raise_path("statfs", path);
    return xt_undefined();
  }
  xt_object_set(object, xt_string_from_cstr("type"), xt_number((double)info.f_type));
  xt_object_set(object, xt_string_from_cstr("bsize"), xt_number((double)info.f_bsize));
  xt_object_set(object, xt_string_from_cstr("blocks"), xt_number((double)info.f_blocks));
  xt_object_set(object, xt_string_from_cstr("bfree"), xt_number((double)info.f_bfree));
  xt_object_set(object, xt_string_from_cstr("bavail"), xt_number((double)info.f_bavail));
  xt_object_set(object, xt_string_from_cstr("files"), xt_number((double)info.f_files));
  xt_object_set(object, xt_string_from_cstr("ffree"), xt_number((double)info.f_ffree));
#endif
  return object;
}

/* ------------------------------------------------------------------------- */
/* mkdtempSync                                                               */
/* ------------------------------------------------------------------------- */

xt_value xt_node_mkdtemp(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *prefix = xt_node_cstr(argv[0]);
  if (!prefix) {
    xt_fs_raise_errno(EINVAL, "mkdtemp", NULL);
    return xt_undefined();
  }
  size_t length = strlen(prefix);
  char *dir = (char *)malloc(length + 7);
  if (!dir) return xt_undefined();
  memcpy(dir, prefix, length);
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  for (int attempt = 0; attempt < 100; attempt++) {
    unsigned int seed = (unsigned int)(time(NULL) ^ (attempt * 2654435761u));
    for (int i = 0; i < 6; i++) dir[length + i] = alphabet[seed % 36u], seed = seed * 1103515245u + 12345u;
    dir[length + 6] = '\0';
    if (xt_fs_mkdir_one(dir) == 0) {
      xt_value result = xt_string_from_cstr(dir);
      free(dir);
      return result;
    }
    if (errno != EEXIST) break;
  }
  free(dir);
  xt_fs_raise_path("mkdtemp", prefix);
  return xt_undefined();
}
