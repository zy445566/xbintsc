/*
 * Node.js `fs/promises` for xbintsc.
 *
 * xbintsc has no asynchronous I/O scheduler, so these builtins reuse the
 * synchronous `fs` implementations and wrap the result in a promise that is
 * already settled. `await`/`.then` therefore work as expected while the actual
 * work happens synchronously. A synchronous failure is turned into a rejected
 * promise instead of a thrown exception (matching Node's contract).
 *
 * `open()` resolves to a `FileHandle`-shaped object (a plain object with an
 * `fd` property and the usual methods); there is no shared prototype yet, so
 * `handle instanceof FileHandle` is not supported.
 */

#include "../node_common.h"
#include "fs_common.h"

/* ------------------------------------------------------------------------- */
/* FileHandle                                                                */
/* ------------------------------------------------------------------------- */

static xt_value xt_fh_method(xt_value thisValue, xt_value (*fn)(int32_t, xt_value *), int32_t argc, xt_value *argv,
                             int prefix) {
  xt_value args[10];
  int32_t count = 0;
  if (prefix >= 1) args[count++] = xt_object_get_cstr(thisValue, "fd");
  if (prefix >= 2) args[count++] = xt_object_get_cstr(thisValue, "fd2");
  for (int32_t i = 0; i < argc && count < 10; i++) args[count++] = argv[i];
  return xt_fs_promisify(fn, count, args);
}

static xt_value xt_fh_read(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_fh_method(thisValue, xt_node_read, argc, argv, 1);
}

static xt_value xt_fh_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_fh_method(thisValue, xt_node_write, argc, argv, 1);
}

static xt_value xt_fh_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_fh_method(thisValue, xt_node_close, argc, argv, 1);
}

static xt_value xt_fh_stat(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_fh_method(thisValue, xt_node_fstat, argc, argv, 1);
}

static xt_value xt_fh_truncate(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_fh_method(thisValue, xt_node_ftruncate, argc, argv, 1);
}

static xt_value xt_fh_chmod(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_fh_method(thisValue, xt_node_fchmod, argc, argv, 1);
}

static xt_value xt_fh_chown(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_fh_method(thisValue, xt_node_fchown, argc, argv, 1);
}

static xt_value xt_fh_utimes(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_fh_method(thisValue, xt_node_futimes, argc, argv, 1);
}

static xt_value xt_fh_sync(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_fh_method(thisValue, xt_node_fsync, argc, argv, 1);
}

static xt_value xt_fh_datasync(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_fh_method(thisValue, xt_node_fdatasync, argc, argv, 1);
}

/* Read from the handle's current position to EOF; caller frees. */
static unsigned char *xt_fh_slurp(int fd, size_t *outLength) {
  size_t capacity = 8192;
  size_t length = 0;
  unsigned char *bytes = (unsigned char *)malloc(capacity);
  if (!bytes) return NULL;
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      unsigned char *grown = (unsigned char *)realloc(bytes, capacity);
      if (!grown) {
        free(bytes);
        return NULL;
      }
      bytes = grown;
    }
    ssize_t bytesRead = XT_FS_READ(fd, bytes + length, capacity - length);
    if (bytesRead < 0) {
      free(bytes);
      return NULL;
    }
    if (bytesRead == 0) break;
    length += (size_t)bytesRead;
  }
  *outLength = length;
  return bytes;
}

static xt_value xt_fh_read_all(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int fd = (int)xt_to_number(xt_object_get_cstr(thisValue, "fd"));
  const char *encoding = argc > 0 ? xt_fs_encoding(argv[0]) : NULL;
  size_t length = 0;
  unsigned char *bytes = xt_fh_slurp(fd, &length);
  if (!bytes) return xt_promise_reject(xt_fs_error_object(errno, "read", NULL));
  xt_value result = xt_fs_encode_bytes(bytes, length, encoding);
  free(bytes);
  return xt_promise_resolve(result);
}

static xt_value xt_fh_write_all(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value args[2];
  args[0] = xt_object_get_cstr(thisValue, "fd");
  args[1] = argc > 0 ? argv[0] : XT_UNDEFINED;
  return xt_fs_promisify(xt_node_write, 2, args);
}

static xt_value xt_fh_new(xt_value fdValue) {
  xt_value handle = xt_object_new();
  xt_node_set(handle, "fd", fdValue);
  xt_node_define_method(handle, "read", (void *)xt_fh_read);
  xt_node_define_method(handle, "write", (void *)xt_fh_write);
  xt_node_define_method(handle, "readFile", (void *)xt_fh_read_all);
  xt_node_define_method(handle, "writeFile", (void *)xt_fh_write_all);
  xt_node_define_method(handle, "appendFile", (void *)xt_fh_write_all);
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

/* ------------------------------------------------------------------------- */
/* Promise wrappers                                                          */
/* ------------------------------------------------------------------------- */

#define XT_FS_PROMISE(name, fn)                                                                    \
  xt_value name(int32_t argc, xt_value *argv) { return xt_fs_promisify((fn), argc, argv); }

XT_FS_PROMISE(xt_node_p_read_file, xt_node_read_text_file)
XT_FS_PROMISE(xt_node_p_write_file, xt_node_write_file)
XT_FS_PROMISE(xt_node_p_append_file, xt_node_append_file)
XT_FS_PROMISE(xt_node_p_mkdir, xt_node_mkdir)
XT_FS_PROMISE(xt_node_p_readdir, xt_node_read_dir)
XT_FS_PROMISE(xt_node_p_rm, xt_node_rm)
XT_FS_PROMISE(xt_node_p_unlink, xt_node_unlink)
XT_FS_PROMISE(xt_node_p_rmdir, xt_node_rmdir)
XT_FS_PROMISE(xt_node_p_rename, xt_node_rename)
XT_FS_PROMISE(xt_node_p_copy_file, xt_node_copy_file)
XT_FS_PROMISE(xt_node_p_realpath, xt_node_realpath)
XT_FS_PROMISE(xt_node_p_stat, xt_node_stat)
XT_FS_PROMISE(xt_node_p_lstat, xt_node_lstat)
XT_FS_PROMISE(xt_node_p_access, xt_node_access)
XT_FS_PROMISE(xt_node_p_cp, xt_node_cp)
XT_FS_PROMISE(xt_node_p_chmod, xt_node_chmod)
XT_FS_PROMISE(xt_node_p_chown, xt_node_chown)
XT_FS_PROMISE(xt_node_p_lchmod, xt_node_lchmod)
XT_FS_PROMISE(xt_node_p_lchown, xt_node_lchown)
XT_FS_PROMISE(xt_node_p_truncate, xt_node_truncate)
XT_FS_PROMISE(xt_node_p_utimes, xt_node_utimes)
XT_FS_PROMISE(xt_node_p_lutimes, xt_node_lutimes)
XT_FS_PROMISE(xt_node_p_link, xt_node_link)
XT_FS_PROMISE(xt_node_p_symlink, xt_node_symlink)
XT_FS_PROMISE(xt_node_p_readlink, xt_node_readlink)
XT_FS_PROMISE(xt_node_p_mkdtemp, xt_node_mkdtemp)
XT_FS_PROMISE(xt_node_p_statfs, xt_node_statfs)
XT_FS_PROMISE(xt_node_p_opendir, xt_node_opendir)
XT_FS_PROMISE(xt_node_p_glob, xt_node_glob)
XT_FS_PROMISE(xt_node_p_watch, xt_node_watch)

xt_value xt_node_p_open(int32_t argc, xt_value *argv) {
  void *frame = xt_try_enter();
  if (xt_try_setjmp(frame) == 0) {
    xt_value fdValue = xt_node_open(argc, argv);
    xt_try_leave(frame);
    return xt_promise_resolve(xt_fh_new(fdValue));
  }
  xt_value exception = xt_try_exception(frame);
  xt_try_leave(frame);
  return xt_promise_reject(exception);
}

/* ------------------------------------------------------------------------- */
/* `fs.promises` facade                                                      */
/* ------------------------------------------------------------------------- */

#define XT_FS_PROMISES_FACADE(name, object)                                                        \
  static xt_value xt_fs_promises_facade_##name(xt_value thisValue, xt_value env, int32_t argc,     \
                                               xt_value *argv) {                                   \
    (void)thisValue;                                                                               \
    (void)env;                                                                                     \
    return name(argc, argv);                                                                       \
  }

XT_FS_PROMISES_FACADE(xt_node_p_read_file, _)
XT_FS_PROMISES_FACADE(xt_node_p_write_file, _)
XT_FS_PROMISES_FACADE(xt_node_p_append_file, _)
XT_FS_PROMISES_FACADE(xt_node_p_mkdir, _)
XT_FS_PROMISES_FACADE(xt_node_p_readdir, _)
XT_FS_PROMISES_FACADE(xt_node_p_rm, _)
XT_FS_PROMISES_FACADE(xt_node_p_unlink, _)
XT_FS_PROMISES_FACADE(xt_node_p_rmdir, _)
XT_FS_PROMISES_FACADE(xt_node_p_rename, _)
XT_FS_PROMISES_FACADE(xt_node_p_copy_file, _)
XT_FS_PROMISES_FACADE(xt_node_p_realpath, _)
XT_FS_PROMISES_FACADE(xt_node_p_stat, _)
XT_FS_PROMISES_FACADE(xt_node_p_lstat, _)
XT_FS_PROMISES_FACADE(xt_node_p_access, _)
XT_FS_PROMISES_FACADE(xt_node_p_cp, _)
XT_FS_PROMISES_FACADE(xt_node_p_chmod, _)
XT_FS_PROMISES_FACADE(xt_node_p_chown, _)
XT_FS_PROMISES_FACADE(xt_node_p_lchmod, _)
XT_FS_PROMISES_FACADE(xt_node_p_lchown, _)
XT_FS_PROMISES_FACADE(xt_node_p_truncate, _)
XT_FS_PROMISES_FACADE(xt_node_p_utimes, _)
XT_FS_PROMISES_FACADE(xt_node_p_lutimes, _)
XT_FS_PROMISES_FACADE(xt_node_p_link, _)
XT_FS_PROMISES_FACADE(xt_node_p_symlink, _)
XT_FS_PROMISES_FACADE(xt_node_p_readlink, _)
XT_FS_PROMISES_FACADE(xt_node_p_mkdtemp, _)
XT_FS_PROMISES_FACADE(xt_node_p_statfs, _)
XT_FS_PROMISES_FACADE(xt_node_p_opendir, _)
XT_FS_PROMISES_FACADE(xt_node_p_glob, _)
XT_FS_PROMISES_FACADE(xt_node_p_watch, _)
XT_FS_PROMISES_FACADE(xt_node_p_open, _)

xt_value xt_fs_promises(void) {
  static xt_value cached = 0;
  if (XT_IS_OBJECT(cached)) return cached;
  xt_value object = xt_object_new();
#define XT_FS_PROMISE_METHOD(jsName, name)                                                         \
  xt_node_define_method(object, jsName, (void *)xt_fs_promises_facade_##name)
  XT_FS_PROMISE_METHOD("readFile", xt_node_p_read_file);
  XT_FS_PROMISE_METHOD("writeFile", xt_node_p_write_file);
  XT_FS_PROMISE_METHOD("appendFile", xt_node_p_append_file);
  XT_FS_PROMISE_METHOD("mkdir", xt_node_p_mkdir);
  XT_FS_PROMISE_METHOD("readdir", xt_node_p_readdir);
  XT_FS_PROMISE_METHOD("rm", xt_node_p_rm);
  XT_FS_PROMISE_METHOD("unlink", xt_node_p_unlink);
  XT_FS_PROMISE_METHOD("rmdir", xt_node_p_rmdir);
  XT_FS_PROMISE_METHOD("rename", xt_node_p_rename);
  XT_FS_PROMISE_METHOD("copyFile", xt_node_p_copy_file);
  XT_FS_PROMISE_METHOD("realpath", xt_node_p_realpath);
  XT_FS_PROMISE_METHOD("stat", xt_node_p_stat);
  XT_FS_PROMISE_METHOD("lstat", xt_node_p_lstat);
  XT_FS_PROMISE_METHOD("access", xt_node_p_access);
  XT_FS_PROMISE_METHOD("cp", xt_node_p_cp);
  XT_FS_PROMISE_METHOD("chmod", xt_node_p_chmod);
  XT_FS_PROMISE_METHOD("chown", xt_node_p_chown);
  XT_FS_PROMISE_METHOD("lchmod", xt_node_p_lchmod);
  XT_FS_PROMISE_METHOD("lchown", xt_node_p_lchown);
  XT_FS_PROMISE_METHOD("truncate", xt_node_p_truncate);
  XT_FS_PROMISE_METHOD("utimes", xt_node_p_utimes);
  XT_FS_PROMISE_METHOD("lutimes", xt_node_p_lutimes);
  XT_FS_PROMISE_METHOD("link", xt_node_p_link);
  XT_FS_PROMISE_METHOD("symlink", xt_node_p_symlink);
  XT_FS_PROMISE_METHOD("readlink", xt_node_p_readlink);
  XT_FS_PROMISE_METHOD("mkdtemp", xt_node_p_mkdtemp);
  XT_FS_PROMISE_METHOD("statfs", xt_node_p_statfs);
  XT_FS_PROMISE_METHOD("opendir", xt_node_p_opendir);
  XT_FS_PROMISE_METHOD("glob", xt_node_p_glob);
  XT_FS_PROMISE_METHOD("watch", xt_node_p_watch);
  XT_FS_PROMISE_METHOD("open", xt_node_p_open);
#undef XT_FS_PROMISE_METHOD
  xt_object_set(object, xt_string_from_cstr("constants"), xt_fs_constants());
  cached = object;
  return cached;
}
