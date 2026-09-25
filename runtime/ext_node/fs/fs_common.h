/*
 * Shared helpers for the xbintsc Node `fs` runtime.
 *
 * The `fs` builtins live in several translation units (read_file.c,
 * write_file.c, fs_ops.c) but agree on encoding handling and error reporting
 * through these static inline helpers. Encoding support is deliberately
 * buffer-free: `base64`/`hex` are encoded to strings directly, since xbintsc
 * has no `Buffer` value type yet.
 */
#ifndef XT_NODE_FS_COMMON_H
#define XT_NODE_FS_COMMON_H

#include "rt_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* -- platform shims ------------------------------------------------------- */

#if defined(_WIN32)
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Stat results are filled from Win32 directly. The CRT's `_stat` converts the
 * FILETIME through the *local* time zone before producing `time_t`, so a time
 * near the Unix epoch (e.g. `utimesSync(path, 1000, 2000)`) maps to 1969 on a
 * machine west of UTC and the CRT reports an unrepresentable `st_mtime` (-1).
 * Reading the Win32 FILETIME and converting it to Unix seconds in UTC like
 * libuv/Node keeps the values exact and timezone independent. */
typedef struct {
  long long st_size;
  unsigned int st_mode;
  unsigned int st_uid;
  unsigned int st_gid;
  unsigned int st_dev;
  unsigned long long st_ino;
  unsigned int st_nlink;
  unsigned int st_rdev;
  long long st_mtime; /* seconds since the Unix epoch */
  long long st_atime;
  long long st_ctime;
} xt_fs_stat_t;

int xt_fs_win_stat(const char *path, xt_fs_stat_t *out);
int xt_fs_win_lstat(const char *path, xt_fs_stat_t *out);
int xt_fs_win_fstat(int fd, xt_fs_stat_t *out);
#define xt_fs_stat_fn xt_fs_win_stat
#define xt_fs_lstat_fn xt_fs_win_lstat
#define xt_fs_fstat_fn xt_fs_win_fstat
#define xt_fs_access _access
#define xt_fs_mkdir_mode(path, mode) _mkdir(path)
#define xt_fs_mkdir_one(path) _mkdir(path)
#define xt_fs_rmdir_one(path) _rmdir(path)
#define XT_FS_OPEN _open
#define XT_FS_CLOSE _close
#define XT_FS_READ _read
#define XT_FS_WRITE _write

#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) 0
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(m) 0
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(m) 0
#endif
#ifndef S_ISBLK
#define S_ISBLK(m) 0
#endif
#ifndef S_ISCHR
#define S_ISCHR(m) (((m) & _S_IFMT) == _S_IFCHR)
#endif
#ifndef F_OK
#define F_OK 0
#endif
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct stat xt_fs_stat_t;
#define xt_fs_stat_fn stat
#define xt_fs_lstat_fn lstat
#define xt_fs_access access
#define xt_fs_mkdir_mode(path, mode) mkdir((path), (mode))
#define xt_fs_mkdir_one(path) mkdir(path, 0777)
#define xt_fs_rmdir_one(path) rmdir(path)
#define XT_FS_OPEN open
#define XT_FS_CLOSE close
#define XT_FS_READ read
#define XT_FS_WRITE write
#endif

/* -- directory iteration -------------------------------------------------- */

#if defined(_WIN32)
/* Directory iteration backed by the CRT's _findfirst/_findnext. */
typedef struct {
  intptr_t handle;
  struct _finddata_t entry;
} xt_fs_dir;

static inline int xt_fs_dir_open(xt_fs_dir *dir, const char *path) {
  size_t length = strlen(path);
  char *pattern = (char *)malloc(length + 3);
  if (!pattern) return 0;
  memcpy(pattern, path, length);
  pattern[length] = '/';
  pattern[length + 1] = '*';
  pattern[length + 2] = '\0';
  dir->handle = _findfirst(pattern, &dir->entry);
  free(pattern);
  return dir->handle != -1;
}

static inline int xt_fs_dir_next(xt_fs_dir *dir) { return _findnext(dir->handle, &dir->entry) == 0; }
static inline const char *xt_fs_dir_name(xt_fs_dir *dir) { return dir->entry.name; }
static inline void xt_fs_dir_close(xt_fs_dir *dir) { _findclose(dir->handle); }
#else
#include <dirent.h>

typedef struct {
  DIR *handle;
  struct dirent *entry;
} xt_fs_dir;

static inline int xt_fs_dir_open(xt_fs_dir *dir, const char *path) {
  dir->handle = opendir(path);
  dir->entry = NULL;
  return dir->handle != NULL;
}

static inline int xt_fs_dir_next(xt_fs_dir *dir) {
  dir->entry = readdir(dir->handle);
  return dir->entry != NULL;
}

static inline const char *xt_fs_dir_name(xt_fs_dir *dir) { return dir->entry->d_name; }
static inline void xt_fs_dir_close(xt_fs_dir *dir) { closedir(dir->handle); }
#endif

/* -- errors --------------------------------------------------------------- */

/* Node's `err.code` for the errno values the fs module can raise. */
static inline const char *xt_fs_errno_name(int err) {
  switch (err) {
    case ENOENT: return "ENOENT";
    case EACCES: return "EACCES";
    case EEXIST: return "EEXIST";
    case ENOTDIR: return "ENOTDIR";
    case EISDIR: return "EISDIR";
    case EPERM: return "EPERM";
    case EINVAL: return "EINVAL";
    case EBADF: return "EBADF";
    case ENOSPC: return "ENOSPC";
    case EROFS: return "EROFS";
    case EMFILE: return "EMFILE";
#ifdef ENOTEMPTY
    case ENOTEMPTY: return "ENOTEMPTY";
#endif
#ifdef ELOOP
    case ELOOP: return "ELOOP";
#endif
#ifdef ENAMETOOLONG
    case ENAMETOOLONG: return "ENAMETOOLONG";
#endif
#ifdef EXDEV
    case EXDEV: return "EXDEV";
#endif
    default: return "UNKNOWN";
  }
}

/* Build a Node-shaped Error (`code`/`errno`/`syscall`/`path`/`message`). */
static inline xt_value xt_fs_error_object(int err, const char *syscall, const char *path) {
  const char *code = xt_fs_errno_name(err);
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "%s: %s, %s '%s'", code, strerror(err), syscall ? syscall : "",
           path ? path : "");
  xt_value error = xt_object_new();
  ((xt_object *)XT_GET_PTR(error))->header.kind = XT_OBJECT_KIND_ERROR;
  xt_object_set(error, xt_string_from_cstr("name"), xt_string_from_cstr("Error"));
  xt_object_set(error, xt_string_from_cstr("message"), xt_string_from_cstr(buffer));
  xt_object_set(error, xt_string_from_cstr("code"), xt_string_from_cstr(code));
  xt_object_set(error, xt_string_from_cstr("errno"), xt_number((double)(-err)));
  if (syscall) xt_object_set(error, xt_string_from_cstr("syscall"), xt_string_from_cstr(syscall));
  if (path) xt_object_set(error, xt_string_from_cstr("path"), xt_string_from_cstr(path));
  return error;
}

/* Raise `errno` (captured by the caller right after a failed syscall). */
static inline void xt_fs_raise_errno(int err, const char *syscall, const char *path) {
  xt_throw(xt_fs_error_object(err, syscall, path));
}

static inline void xt_fs_raise_path(const char *syscall, const char *path) {
  xt_fs_raise_errno(errno, syscall, path);
}

/* -- time helpers --------------------------------------------------------- */

/* Node accepts a Date or a number of seconds; Date stores milliseconds. */
static inline double xt_fs_time_seconds(xt_value value) {
  if (XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_DATE) {
    return ((xt_date *)XT_GET_PTR(value))->time / 1000.0;
  }
  return xt_to_number(value);
}

/* -- open-flag parsing ---------------------------------------------------- */

#if defined(_WIN32)
#define XT_FS_BINARY_FLAG _O_BINARY
#define XT_FS_RDONLY _O_RDONLY
#define XT_FS_WRONLY _O_WRONLY
#define XT_FS_RDWR _O_RDWR
#define XT_FS_CREAT _O_CREAT
#define XT_FS_TRUNC _O_TRUNC
#define XT_FS_APPEND _O_APPEND
#define XT_FS_EXCL _O_EXCL
#else
#define XT_FS_BINARY_FLAG 0
#define XT_FS_RDONLY O_RDONLY
#define XT_FS_WRONLY O_WRONLY
#define XT_FS_RDWR O_RDWR
#define XT_FS_CREAT O_CREAT
#define XT_FS_TRUNC O_TRUNC
#define XT_FS_APPEND O_APPEND
#define XT_FS_EXCL O_EXCL
#endif

/* Translate a Node flag string (`"r"`, `"w+"`, `"a"`, ...) or number. */
static inline int xt_fs_parse_flags(xt_value value) {
  if (XT_IS_STRING(value)) {
    const char *flag = xt_string_data(value);
    if (!flag) return XT_FS_RDONLY | XT_FS_BINARY_FLAG;
    int plus = strchr(flag, '+') != NULL;
    int excl = strchr(flag, 'x') != NULL;
    int base;
    if (flag[0] == 'a') base = XT_FS_APPEND | XT_FS_CREAT;
    else if (flag[0] == 'w') base = XT_FS_CREAT | XT_FS_TRUNC;
    else base = 0; /* 'r' and the numeric path below */
    base |= (plus || flag[0] == 'w' || flag[0] == 'a') ? XT_FS_RDWR : XT_FS_RDONLY;
    if (excl) base |= XT_FS_EXCL;
    return base | XT_FS_BINARY_FLAG;
  }
  return (int)xt_to_number(value) | XT_FS_BINARY_FLAG;
}

/* -- shared object builders (defined in fs_ops.c) ------------------------- */

xt_value xt_node_stat_result(const xt_fs_stat_t *info);
xt_value xt_node_dirent_result(const char *dir, const char *name);

/* -- synchronous fs builtins (one translation unit per group) ------------- */

xt_value xt_node_read_text_file(int32_t argc, xt_value *argv);
xt_value xt_node_write_file(int32_t argc, xt_value *argv);
xt_value xt_node_append_file(int32_t argc, xt_value *argv);
xt_value xt_node_exists(int32_t argc, xt_value *argv);
xt_value xt_node_mkdir(int32_t argc, xt_value *argv);
xt_value xt_node_read_dir(int32_t argc, xt_value *argv);
xt_value xt_node_rm(int32_t argc, xt_value *argv);
xt_value xt_node_unlink(int32_t argc, xt_value *argv);
xt_value xt_node_rmdir(int32_t argc, xt_value *argv);
xt_value xt_node_rename(int32_t argc, xt_value *argv);
xt_value xt_node_copy_file(int32_t argc, xt_value *argv);
xt_value xt_node_realpath(int32_t argc, xt_value *argv);
xt_value xt_node_stat(int32_t argc, xt_value *argv);
xt_value xt_node_lstat(int32_t argc, xt_value *argv);
xt_value xt_node_cp(int32_t argc, xt_value *argv);

/* fd_ops.c */
xt_value xt_node_open(int32_t argc, xt_value *argv);
xt_value xt_node_close(int32_t argc, xt_value *argv);
xt_value xt_node_read(int32_t argc, xt_value *argv);
xt_value xt_node_write(int32_t argc, xt_value *argv);
xt_value xt_node_readv(int32_t argc, xt_value *argv);
xt_value xt_node_writev(int32_t argc, xt_value *argv);
xt_value xt_node_fstat(int32_t argc, xt_value *argv);
xt_value xt_node_fsync(int32_t argc, xt_value *argv);
xt_value xt_node_fdatasync(int32_t argc, xt_value *argv);
xt_value xt_node_ftruncate(int32_t argc, xt_value *argv);
xt_value xt_node_fchmod(int32_t argc, xt_value *argv);
xt_value xt_node_fchown(int32_t argc, xt_value *argv);
xt_value xt_node_futimes(int32_t argc, xt_value *argv);

/* meta_ops.c */
xt_value xt_node_access(int32_t argc, xt_value *argv);
xt_value xt_node_chmod(int32_t argc, xt_value *argv);
xt_value xt_node_chown(int32_t argc, xt_value *argv);
xt_value xt_node_lchmod(int32_t argc, xt_value *argv);
xt_value xt_node_lchown(int32_t argc, xt_value *argv);
xt_value xt_node_truncate(int32_t argc, xt_value *argv);
xt_value xt_node_utimes(int32_t argc, xt_value *argv);
xt_value xt_node_lutimes(int32_t argc, xt_value *argv);
xt_value xt_node_statfs(int32_t argc, xt_value *argv);
xt_value xt_node_mkdtemp(int32_t argc, xt_value *argv);

/* link_ops.c */
xt_value xt_node_link(int32_t argc, xt_value *argv);
xt_value xt_node_symlink(int32_t argc, xt_value *argv);
xt_value xt_node_readlink(int32_t argc, xt_value *argv);

/* dir.c */
xt_value xt_node_opendir(int32_t argc, xt_value *argv);

/* glob.c */
xt_value xt_node_glob(int32_t argc, xt_value *argv);

/* watch.c */
xt_value xt_node_watch(int32_t argc, xt_value *argv);
xt_value xt_node_watch_file(int32_t argc, xt_value *argv);
xt_value xt_node_unwatch_file(int32_t argc, xt_value *argv);

/* constants.c */
xt_value xt_fs_constants(void);
xt_value xt_fs_promises(void);

/* A promise wrapper over a synchronous builtin: an immediately settled
 * promise that is rejected (rather than throwing) when the builtin raises. */
static inline xt_value xt_fs_promisify(xt_value (*fn)(int32_t, xt_value *), int32_t argc, xt_value *argv) {
  void *frame = xt_try_enter();
  if (xt_try_setjmp(frame) == 0) {
    xt_value result = fn(argc, argv);
    xt_try_leave(frame);
    return xt_promise_resolve(result);
  }
  xt_value exception = xt_try_exception(frame);
  xt_try_leave(frame);
  return xt_promise_reject(exception);
}

/* -- Buffer bridge (defined in buffer/parts/prototype.inc) ---------------- */

int xt_node_is_buffer(xt_value value);
unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength);
uint32_t xt_node_buffer_length(xt_value value);
void xt_node_buffer_set(xt_value value, uint32_t index, unsigned char byte);

static const char xt_fs_base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Accepts a bare encoding string or an options object with `encoding`. */
static inline const char *xt_fs_encoding(xt_value options) {
  if (XT_IS_STRING(options)) return xt_string_data(options);
  if (XT_IS_OBJECT(options)) {
    xt_value encoding = xt_object_get_cstr(options, "encoding");
    if (XT_IS_STRING(encoding)) return xt_string_data(encoding);
  }
  return NULL;
}

static inline int xt_fs_is_hex(const char *encoding) {
  return encoding && strcmp(encoding, "hex") == 0;
}

static inline int xt_fs_is_base64(const char *encoding) {
  return encoding && strcmp(encoding, "base64") == 0;
}

static inline int xt_fs_is_base64url(const char *encoding) {
  return encoding && strcmp(encoding, "base64url") == 0;
}

static inline int xt_fs_is_utf16(const char *encoding) {
  return encoding && (strcmp(encoding, "utf16le") == 0 || strcmp(encoding, "ucs2") == 0 ||
                      strcmp(encoding, "ucs-2") == 0 || strcmp(encoding, "utf-16le") == 0);
}

static inline xt_value xt_fs_encode_hex(const unsigned char *data, size_t length) {
  static const char digits[] = "0123456789abcdef";
  char *buffer = (char *)malloc(length * 2 + 1);
  if (!buffer) return xt_undefined();
  for (size_t i = 0; i < length; i++) {
    buffer[i * 2] = digits[data[i] >> 4];
    buffer[i * 2 + 1] = digits[data[i] & 0x0f];
  }
  xt_value result = xt_string_new(buffer, length * 2);
  free(buffer);
  return result;
}

static inline xt_value xt_fs_encode_base64(const unsigned char *data, size_t length) {
  size_t outLength = ((length + 2) / 3) * 4;
  char *buffer = (char *)malloc(outLength + 1);
  if (!buffer) return xt_undefined();
  size_t i = 0, j = 0;
  while (i + 2 < length) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | (uint32_t)data[i + 2];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 6) & 63];
    buffer[j++] = xt_fs_base64_alphabet[n & 63];
    i += 3;
  }
  if (length - i == 1) {
    uint32_t n = (uint32_t)data[i] << 16;
    buffer[j++] = xt_fs_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = '=';
    buffer[j++] = '=';
  } else if (length - i == 2) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
    buffer[j++] = xt_fs_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 6) & 63];
    buffer[j++] = '=';
  }
  xt_value result = xt_string_new(buffer, j);
  free(buffer);
  return result;
}

/* Encode bytes for `readFileSync` according to `encoding` (default raw UTF-8). */
static inline xt_value xt_fs_encode_base64url(const unsigned char *data, size_t length) {
  size_t outLength = ((length + 2) / 3) * 4 + 1;
  char *buffer = (char *)malloc(outLength);
  if (!buffer) return xt_undefined();
  size_t i = 0, j = 0;
  while (i + 2 < length) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | (uint32_t)data[i + 2];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 6) & 63];
    buffer[j++] = xt_fs_base64_alphabet[n & 63];
    i += 3;
  }
  if (length - i == 1) {
    uint32_t n = (uint32_t)data[i] << 16;
    buffer[j++] = xt_fs_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 12) & 63];
  } else if (length - i == 2) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
    buffer[j++] = xt_fs_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 6) & 63];
  }
  for (size_t k = 0; k < j; k++) {
    if (buffer[k] == '+') buffer[k] = '-';
    else if (buffer[k] == '/') buffer[k] = '_';
  }
  xt_value result = xt_string_new(buffer, j);
  free(buffer);
  return result;
}

static inline xt_value xt_fs_encode_bytes(const unsigned char *data, size_t length, const char *encoding) {
  if (xt_fs_is_hex(encoding)) return xt_fs_encode_hex(data, length);
  if (xt_fs_is_base64(encoding)) return xt_fs_encode_base64(data, length);
  if (xt_fs_is_base64url(encoding)) return xt_fs_encode_base64url(data, length);
  return xt_string_new((const char *)data, length);
}

static inline int xt_fs_base64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

static inline int xt_fs_hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/*
 * Decode text for `writeFileSync` according to `encoding`. Returns a malloc'd
 * byte buffer (never NULL unless allocation failed) and stores its length in
 * `*outLength`; the caller frees it.
 */
static inline unsigned char *xt_fs_decode_text(const char *text, size_t length, const char *encoding, size_t *outLength) {
  unsigned char *bytes = NULL;
  if (xt_fs_is_base64(encoding) || xt_fs_is_base64url(encoding)) {
    size_t capacity = (length / 4 + 1) * 3;
    bytes = (unsigned char *)malloc(capacity + 1);
    if (!bytes) return NULL;
    size_t written = 0;
    int accumulator = 0, bits = 0;
    for (size_t i = 0; i < length; i++) {
      if (text[i] == '=') break;
      int value = xt_fs_base64_value(text[i]);
      if (value < 0) continue;
      accumulator = (accumulator << 6) | value;
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        bytes[written++] = (unsigned char)((accumulator >> bits) & 0xff);
      }
    }
    bytes[written] = '\0';
    *outLength = written;
    return bytes;
  }
  if (xt_fs_is_hex(encoding)) {
    bytes = (unsigned char *)malloc(length / 2 + 1);
    if (!bytes) return NULL;
    size_t written = 0;
    int high = -1;
    for (size_t i = 0; i < length; i++) {
      int value = xt_fs_hex_value(text[i]);
      if (value < 0) continue;
      if (high < 0) {
        high = value;
      } else {
        bytes[written++] = (unsigned char)((high << 4) | value);
        high = -1;
      }
    }
    bytes[written] = '\0';
    *outLength = written;
    return bytes;
  }
  bytes = (unsigned char *)malloc(length + 1);
  if (!bytes) return NULL;
  if (length > 0) memcpy(bytes, text, length);
  bytes[length] = '\0';
  *outLength = length;
  return bytes;
}

/*
 * Report a failed syscall. The fs module follows Node and *throws* a shaped
 * Error (the runtime now has catchable exceptions), so callers must not rely
 * on a return value after a failure.
 */
static inline void xt_fs_error(const char *action, const char *path) {
  xt_fs_raise_errno(errno, action, path);
}

#endif /* XT_NODE_FS_COMMON_H */
