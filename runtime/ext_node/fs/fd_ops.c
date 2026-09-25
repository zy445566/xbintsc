/*
 * Node.js `fs` file-descriptor operations for xbintsc.
 *
 *     openSync, closeSync, readSync, writeSync, readvSync, writevSync,
 *     fstatSync, fsyncSync, fdatasyncSync, ftruncateSync, fchmodSync,
 *     fchownSync, futimesSync
 *
 * Buffers are the runtime's plain-object representation; the byte bridge lives
 * in `buffer/parts/prototype.inc` (`xt_node_buffer_length`/`xt_node_buffer_set`).
 * Errors throw Node-shaped `Error` objects (see `fs_common.h`).
 */

#include "../node_common.h"
#include "fs_common.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#if !defined(_WIN32)
#include <sys/time.h>
#endif

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#define xt_fs_fsync _commit
#define xt_fs_fdatasync _commit
#define xt_fs_ftruncate(fd, length) _chsize_s((fd), (long long)(length))
#else
#include <unistd.h>
#define xt_fs_fstat_fn fstat
#define xt_fs_fsync fsync
#if defined(__APPLE__)
#define xt_fs_fdatasync fsync
#else
#define xt_fs_fdatasync fdatasync
#endif
#define xt_fs_ftruncate(fd, length) ftruncate((fd), (off_t)(length))
#endif

#if defined(_WIN32)
/* See meta_ops.c: set times through `SetFileTime` so the conversion is exact
 * and does not depend on the CRT's local-time `_utimbuf` ABI. */
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

/* Bytes backing a string or Buffer value; caller frees. */
static unsigned char *xt_fs_value_bytes(xt_value value, size_t *outLength) {
  if (xt_node_is_buffer(value)) return xt_node_buffer_bytes(value, outLength);
  xt_value text = xt_to_string(value);
  xt_string *string = xt_as_string(text);
  size_t length = string ? string->length : 0;
  unsigned char *bytes = (unsigned char *)malloc(length ? length : 1);
  if (!bytes) {
    *outLength = 0;
    return NULL;
  }
  if (length > 0) memcpy(bytes, string->data, length);
  *outLength = length;
  return bytes;
}

/* ------------------------------------------------------------------------- */
/* openSync / closeSync                                                      */
/* ------------------------------------------------------------------------- */

xt_value xt_node_open(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_node_cstr(argv[0]);
  if (!path) {
    xt_fs_raise_errno(EINVAL, "open", NULL);
    return xt_undefined();
  }
  int flags = argc > 1 ? xt_fs_parse_flags(argv[1]) : (XT_FS_RDONLY | XT_FS_BINARY_FLAG);
  int mode = argc > 2 ? (int)xt_to_number(argv[2]) : 0666;
#if defined(_WIN32)
  int fd = _open(path, flags, mode);
#else
  int fd = open(path, flags, mode);
#endif
  if (fd < 0) {
    xt_fs_raise_path("open", path);
    return xt_undefined();
  }
  return xt_number((double)fd);
}

xt_value xt_node_close(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  int fd = (int)xt_to_number(argv[0]);
  if (XT_FS_CLOSE(fd) != 0) {
    xt_fs_raise_path("close", NULL);
    return xt_undefined();
  }
  return xt_undefined();
}

/* ------------------------------------------------------------------------- */
/* readSync / writeSync                                                      */
/* ------------------------------------------------------------------------- */

xt_value xt_node_read(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_undefined();
  int fd = (int)xt_to_number(argv[0]);
  xt_value buffer = argv[1];
  if (!xt_node_is_buffer(buffer)) {
    xt_fs_raise_errno(EINVAL, "read", NULL);
    return xt_undefined();
  }
  uint32_t bufferLength = xt_node_buffer_length(buffer);
  uint32_t offset = argc > 2 ? (uint32_t)xt_to_number(argv[2]) : 0;
  uint32_t length = argc > 3 ? (uint32_t)xt_to_number(argv[3]) : (bufferLength > offset ? bufferLength - offset : 0);
  int64_t position =
      argc > 4 && !XT_IS_UNDEFINED(argv[4]) && !XT_IS_NULL(argv[4]) ? (int64_t)xt_to_number(argv[4]) : -1;
  if (length == 0) return xt_number(0);

  unsigned char *temp = (unsigned char *)malloc(length);
  if (!temp) return xt_number(0);
  ssize_t bytesRead = -1;
  if (position >= 0) {
#if defined(_WIN32)
    _lseeki64(fd, position, SEEK_SET);
    bytesRead = _read(fd, temp, (unsigned int)length);
#else
    bytesRead = pread(fd, temp, length, (off_t)position);
#endif
  } else {
    bytesRead = XT_FS_READ(fd, temp, length);
  }
  if (bytesRead < 0) {
    free(temp);
    xt_fs_raise_path("read", NULL);
    return xt_undefined();
  }
  for (ssize_t i = 0; i < bytesRead; i++) xt_node_buffer_set(buffer, offset + (uint32_t)i, temp[i]);
  free(temp);
  return xt_number((double)bytesRead);
}

xt_value xt_node_write(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_undefined();
  int fd = (int)xt_to_number(argv[0]);
  xt_value data = argv[1];
  int isBuffer = xt_node_is_buffer(data);
  size_t length = 0;
  unsigned char *bytes = xt_fs_value_bytes(data, &length);
  if (!bytes) return xt_undefined();

  uint32_t offset = 0;
  uint32_t writeLength = (uint32_t)length;
  int64_t position = -1;
  if (isBuffer) {
    if (argc > 2) offset = (uint32_t)xt_to_number(argv[2]);
    if (argc > 3 && !XT_IS_UNDEFINED(argv[3]) && !XT_IS_NULL(argv[3])) writeLength = (uint32_t)xt_to_number(argv[3]);
    if (argc > 4 && !XT_IS_UNDEFINED(argv[4]) && !XT_IS_NULL(argv[4])) position = (int64_t)xt_to_number(argv[4]);
  } else {
    if (argc > 2 && !XT_IS_UNDEFINED(argv[2]) && !XT_IS_NULL(argv[2])) position = (int64_t)xt_to_number(argv[2]);
  }
  if (offset > length) offset = (uint32_t)length;
  if (writeLength > length - offset) writeLength = (uint32_t)length - offset;
  if (writeLength == 0) {
    free(bytes);
    return xt_number(0);
  }

  ssize_t written = -1;
  if (position >= 0) {
#if defined(_WIN32)
    _lseeki64(fd, position, SEEK_SET);
    written = _write(fd, bytes + offset, (unsigned int)writeLength);
#else
    written = pwrite(fd, bytes + offset, writeLength, (off_t)position);
#endif
  } else {
    written = XT_FS_WRITE(fd, bytes + offset, writeLength);
  }
  free(bytes);
  if (written < 0) {
    xt_fs_raise_path("write", NULL);
    return xt_undefined();
  }
  return xt_number((double)written);
}

/* ------------------------------------------------------------------------- */
/* readvSync / writevSync                                                    */
/* ------------------------------------------------------------------------- */

xt_value xt_node_readv(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_undefined();
  int fd = (int)xt_to_number(argv[0]);
  xt_value buffers = argv[1];
  if (!XT_IS_ARRAY(buffers)) {
    xt_fs_raise_errno(EINVAL, "read", NULL);
    return xt_undefined();
  }
  int64_t position =
      argc > 2 && !XT_IS_UNDEFINED(argv[2]) && !XT_IS_NULL(argv[2]) ? (int64_t)xt_to_number(argv[2]) : -1;
  int32_t count = (int32_t)xt_to_number(xt_array_length(buffers));
  int64_t total = 0;
  for (int32_t i = 0; i < count; i++) {
    xt_value buffer = xt_array_get(buffers, xt_number((double)i));
    if (!xt_node_is_buffer(buffer)) continue;
    uint32_t length = xt_node_buffer_length(buffer);
    if (length == 0) continue;
    unsigned char *temp = (unsigned char *)malloc(length);
    if (!temp) break;
    ssize_t bytesRead = -1;
    if (position >= 0) {
#if defined(_WIN32)
      _lseeki64(fd, position + total, SEEK_SET);
      bytesRead = _read(fd, temp, (unsigned int)length);
#else
      bytesRead = pread(fd, temp, length, (off_t)(position + total));
#endif
    } else {
      bytesRead = XT_FS_READ(fd, temp, length);
    }
    if (bytesRead <= 0) {
      free(temp);
      break;
    }
    for (ssize_t j = 0; j < bytesRead; j++) xt_node_buffer_set(buffer, (uint32_t)j, temp[j]);
    total += bytesRead;
    free(temp);
    if ((uint32_t)bytesRead < length) break;
  }
  return xt_number((double)total);
}

xt_value xt_node_writev(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_undefined();
  int fd = (int)xt_to_number(argv[0]);
  xt_value buffers = argv[1];
  if (!XT_IS_ARRAY(buffers)) {
    xt_fs_raise_errno(EINVAL, "write", NULL);
    return xt_undefined();
  }
  int64_t position =
      argc > 2 && !XT_IS_UNDEFINED(argv[2]) && !XT_IS_NULL(argv[2]) ? (int64_t)xt_to_number(argv[2]) : -1;
  int32_t count = (int32_t)xt_to_number(xt_array_length(buffers));
  int64_t total = 0;
  for (int32_t i = 0; i < count; i++) {
    xt_value buffer = xt_array_get(buffers, xt_number((double)i));
    if (!xt_node_is_buffer(buffer)) continue;
    size_t length = 0;
    unsigned char *bytes = xt_node_buffer_bytes(buffer, &length);
    if (!bytes || length == 0) {
      free(bytes);
      continue;
    }
    ssize_t written = -1;
    if (position >= 0) {
#if defined(_WIN32)
      _lseeki64(fd, position + total, SEEK_SET);
      written = _write(fd, bytes, (unsigned int)length);
#else
      written = pwrite(fd, bytes, length, (off_t)(position + total));
#endif
    } else {
      written = XT_FS_WRITE(fd, bytes, length);
    }
    free(bytes);
    if (written < 0) {
      xt_fs_raise_path("write", NULL);
      return xt_undefined();
    }
    total += written;
    if ((size_t)written < length) break;
  }
  return xt_number((double)total);
}

/* ------------------------------------------------------------------------- */
/* fstatSync / fsyncSync / fdatasyncSync / ftruncateSync                     */
/* ------------------------------------------------------------------------- */

xt_value xt_node_fstat(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  int fd = (int)xt_to_number(argv[0]);
  xt_fs_stat_t info;
  if (xt_fs_fstat_fn(fd, &info) != 0) {
    xt_fs_raise_path("fstat", NULL);
    return xt_undefined();
  }
  return xt_node_stat_result(&info);
}

xt_value xt_node_fsync(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  int fd = (int)xt_to_number(argv[0]);
  if (xt_fs_fsync(fd) != 0) {
    xt_fs_raise_path("fsync", NULL);
    return xt_undefined();
  }
  return xt_undefined();
}

xt_value xt_node_fdatasync(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  int fd = (int)xt_to_number(argv[0]);
  if (xt_fs_fdatasync(fd) != 0) {
    xt_fs_raise_path("fdatasync", NULL);
    return xt_undefined();
  }
  return xt_undefined();
}

xt_value xt_node_ftruncate(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  int fd = (int)xt_to_number(argv[0]);
  int64_t length = argc > 1 ? (int64_t)xt_to_number(argv[1]) : 0;
  if (xt_fs_ftruncate(fd, length) != 0) {
    xt_fs_raise_path("ftruncate", NULL);
    return xt_undefined();
  }
  return xt_undefined();
}

/* ------------------------------------------------------------------------- */
/* fchmodSync / fchownSync / futimesSync                                     */
/* ------------------------------------------------------------------------- */

xt_value xt_node_fchmod(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_undefined();
  int fd = (int)xt_to_number(argv[0]);
  int mode = (int)xt_to_number(argv[1]);
#if defined(_WIN32)
  (void)fd;
  (void)mode;
  return xt_undefined();
#else
  if (fchmod(fd, (mode_t)mode) != 0) {
    xt_fs_raise_path("fchmod", NULL);
    return xt_undefined();
  }
  return xt_undefined();
#endif
}

xt_value xt_node_fchown(int32_t argc, xt_value *argv) {
  if (argc < 3) return xt_undefined();
  int fd = (int)xt_to_number(argv[0]);
  int uid = (int)xt_to_number(argv[1]);
  int gid = (int)xt_to_number(argv[2]);
#if defined(_WIN32)
  (void)fd;
  (void)uid;
  (void)gid;
  return xt_undefined();
#else
  if (fchown(fd, (uid_t)uid, (gid_t)gid) != 0) {
    xt_fs_raise_path("fchown", NULL);
    return xt_undefined();
  }
  return xt_undefined();
#endif
}

xt_value xt_node_futimes(int32_t argc, xt_value *argv) {
  if (argc < 3) return xt_undefined();
  int fd = (int)xt_to_number(argv[0]);
  double atime = xt_fs_time_seconds(argv[1]);
  double mtime = xt_fs_time_seconds(argv[2]);
#if defined(_WIN32)
  intptr_t os_handle = _get_osfhandle(fd);
  if (os_handle == -1 || xt_fs_set_file_times((HANDLE)os_handle, atime, mtime) != 0) {
    xt_fs_raise_path("futime", NULL);
    return xt_undefined();
  }
  return xt_undefined();
#else
  struct timeval times[2];
  times[0].tv_sec = (time_t)atime;
  times[0].tv_usec = (suseconds_t)((atime - (double)times[0].tv_sec) * 1000000.0);
  times[1].tv_sec = (time_t)mtime;
  times[1].tv_usec = (suseconds_t)((mtime - (double)times[1].tv_sec) * 1000000.0);
  if (futimes(fd, times) != 0) {
    xt_fs_raise_path("futimes", NULL);
    return xt_undefined();
  }
  return xt_undefined();
#endif
}
