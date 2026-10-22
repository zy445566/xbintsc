/*
 * `fs.promises` facade for xbintsc.
 *
 * The individual exports are implemented in async_api.c on top of the async I/O
 * pool: their promises settle when a worker finishes, so awaiting them yields
 * to timers, sockets and other suspending work. This file only builds the
 * `fs.promises` namespace object (reachable as `fs.promises` and as the imported
 * `fs/promises` module).
 */

#include "async_job.h"

#define XT_FS_PROMISES_FACADE(name)                                                               \
  static xt_value xt_fs_promises_facade_##name(xt_value thisValue, xt_value env, int32_t argc,    \
                                               xt_value *argv) {                                  \
    (void)thisValue;                                                                              \
    (void)env;                                                                                    \
    return name(argc, argv);                                                                      \
  }

XT_FS_PROMISES_FACADE(xt_node_p_read_file)
XT_FS_PROMISES_FACADE(xt_node_p_write_file)
XT_FS_PROMISES_FACADE(xt_node_p_append_file)
XT_FS_PROMISES_FACADE(xt_node_p_mkdir)
XT_FS_PROMISES_FACADE(xt_node_p_readdir)
XT_FS_PROMISES_FACADE(xt_node_p_rm)
XT_FS_PROMISES_FACADE(xt_node_p_unlink)
XT_FS_PROMISES_FACADE(xt_node_p_rmdir)
XT_FS_PROMISES_FACADE(xt_node_p_rename)
XT_FS_PROMISES_FACADE(xt_node_p_copy_file)
XT_FS_PROMISES_FACADE(xt_node_p_realpath)
XT_FS_PROMISES_FACADE(xt_node_p_stat)
XT_FS_PROMISES_FACADE(xt_node_p_lstat)
XT_FS_PROMISES_FACADE(xt_node_p_access)
XT_FS_PROMISES_FACADE(xt_node_p_cp)
XT_FS_PROMISES_FACADE(xt_node_p_chmod)
XT_FS_PROMISES_FACADE(xt_node_p_chown)
XT_FS_PROMISES_FACADE(xt_node_p_lchmod)
XT_FS_PROMISES_FACADE(xt_node_p_lchown)
XT_FS_PROMISES_FACADE(xt_node_p_truncate)
XT_FS_PROMISES_FACADE(xt_node_p_utimes)
XT_FS_PROMISES_FACADE(xt_node_p_lutimes)
XT_FS_PROMISES_FACADE(xt_node_p_link)
XT_FS_PROMISES_FACADE(xt_node_p_symlink)
XT_FS_PROMISES_FACADE(xt_node_p_readlink)
XT_FS_PROMISES_FACADE(xt_node_p_mkdtemp)
XT_FS_PROMISES_FACADE(xt_node_p_statfs)
XT_FS_PROMISES_FACADE(xt_node_p_opendir)
XT_FS_PROMISES_FACADE(xt_node_p_glob)
XT_FS_PROMISES_FACADE(xt_node_p_watch)
XT_FS_PROMISES_FACADE(xt_node_p_open)

xt_value xt_fs_promises(void) {
  static xt_value cached = 0;
  xt_gc_add_root(&cached);
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
