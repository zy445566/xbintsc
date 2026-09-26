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

/*
 * Report a failed syscall. The fs module follows Node and *throws* a shaped
 * Error (the runtime now has catchable exceptions), so callers must not rely
 * on a return value after a failure.
 */
static inline void xt_fs_error(const char *action, const char *path) {
  xt_fs_raise_errno(errno, action, path);
}
