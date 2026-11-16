/*
 * The callback-style `fs` API (`fs.realpath(path, cb)`, `fs.stat(...)`, …).
 *
 * Node's callback forms run on the same asynchronous I/O pool as
 * `fs.promises`: each one forwards its arguments to the promise-returning
 * implementation and attaches two closures, which call `callback(null, value)`
 * on success and `callback(error)` on failure. Programs written against
 * `require("fs")` callbacks — `proper-lockfile`, most of npm's tooling — work
 * without a promise in sight.
 *
 * `exists` is the exception Node makes: its callback receives a boolean and no
 * error argument.
 */

#include "async_job.h"

/* The promise-returning implementations (async_api.c). */
xt_value xt_node_p_read_file(int32_t argc, xt_value *argv);
xt_value xt_node_p_write_file(int32_t argc, xt_value *argv);
xt_value xt_node_p_append_file(int32_t argc, xt_value *argv);
xt_value xt_node_p_mkdir(int32_t argc, xt_value *argv);
xt_value xt_node_p_readdir(int32_t argc, xt_value *argv);
xt_value xt_node_p_rm(int32_t argc, xt_value *argv);
xt_value xt_node_p_unlink(int32_t argc, xt_value *argv);
xt_value xt_node_p_rmdir(int32_t argc, xt_value *argv);
xt_value xt_node_p_rename(int32_t argc, xt_value *argv);
xt_value xt_node_p_copy_file(int32_t argc, xt_value *argv);
xt_value xt_node_p_cp(int32_t argc, xt_value *argv);
xt_value xt_node_p_realpath(int32_t argc, xt_value *argv);
xt_value xt_node_p_stat(int32_t argc, xt_value *argv);
xt_value xt_node_p_lstat(int32_t argc, xt_value *argv);
xt_value xt_node_p_statfs(int32_t argc, xt_value *argv);
xt_value xt_node_p_access(int32_t argc, xt_value *argv);
xt_value xt_node_p_open(int32_t argc, xt_value *argv);
xt_value xt_node_p_chmod(int32_t argc, xt_value *argv);
xt_value xt_node_p_lchmod(int32_t argc, xt_value *argv);
xt_value xt_node_p_chown(int32_t argc, xt_value *argv);
xt_value xt_node_p_lchown(int32_t argc, xt_value *argv);
xt_value xt_node_p_truncate(int32_t argc, xt_value *argv);
xt_value xt_node_p_utimes(int32_t argc, xt_value *argv);
xt_value xt_node_p_lutimes(int32_t argc, xt_value *argv);
xt_value xt_node_p_link(int32_t argc, xt_value *argv);
xt_value xt_node_p_symlink(int32_t argc, xt_value *argv);
xt_value xt_node_p_readlink(int32_t argc, xt_value *argv);
xt_value xt_node_p_mkdtemp(int32_t argc, xt_value *argv);
xt_value xt_node_p_opendir(int32_t argc, xt_value *argv);
xt_value xt_node_p_glob(int32_t argc, xt_value *argv);

/* `existsSync` from fs_ops.c, for the one callback with a different shape. */
xt_value xt_node_exists(int32_t argc, xt_value *argv);

/* -- the two handlers ----------------------------------------------------- */

static xt_value xt_fs_cb_done(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value callback = xt_closure_env(env, 0);
  xt_value arguments[2] = {XT_NULL, xt_arg(argc, argv, 0)};
  xt_call_with_this(callback, XT_UNDEFINED, 2, arguments);
  return XT_UNDEFINED;
}

static xt_value xt_fs_cb_failed(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value callback = xt_closure_env(env, 0);
  xt_value arguments[1] = {xt_arg(argc, argv, 0)};
  xt_call_with_this(callback, XT_UNDEFINED, 1, arguments);
  return XT_UNDEFINED;
}

static xt_value xt_fs_callbackify(xt_value promise, xt_value callback) {
  xt_value captured[1] = {callback};
  xt_value handlers[2] = {
    xt_closure_new((void *)xt_fs_cb_done, 1, captured),
    xt_closure_new((void *)xt_fs_cb_failed, 1, captured),
  };
  if (!xt_is_promise(promise)) {
    /* A rejected promise would have skipped the handler; nothing to attach. */
    return XT_UNDEFINED;
  }
  int handled = 0;
  xt_value chained = xt_promise_method(promise, "then", 2, handlers, &handled);
  /* The callback consumed the result, so the derivative is not an unhandled
   * rejection the program has to observe. */
  if (xt_is_promise(chained)) xt_promise_observe_value(chained);
  return XT_UNDEFINED;
}

/** Forward every argument but the trailing callback to `target`. */
static xt_value xt_fs_call_with_callback(xt_value (*target)(int32_t, xt_value *), int32_t argc,
                                        xt_value *argv) {
  if (argc < 1 || !XT_IS_FUNCTION(argv[argc - 1])) return target(argc, argv);
  xt_value callback = argv[argc - 1];
  return xt_fs_callbackify(target(argc - 1, argv), callback);
}

#define XT_FS_CALLBACK(name)                                                                       \
  xt_value xt_node_##name##_cb(int32_t argc, xt_value *argv) {                                      \
    return xt_fs_call_with_callback(xt_node_p_##name, argc, argv);                                  \
  }

XT_FS_CALLBACK(read_file)
XT_FS_CALLBACK(write_file)
XT_FS_CALLBACK(append_file)
XT_FS_CALLBACK(mkdir)
XT_FS_CALLBACK(readdir)
XT_FS_CALLBACK(rm)
XT_FS_CALLBACK(unlink)
XT_FS_CALLBACK(rmdir)
XT_FS_CALLBACK(rename)
XT_FS_CALLBACK(copy_file)
XT_FS_CALLBACK(cp)
XT_FS_CALLBACK(realpath)
XT_FS_CALLBACK(stat)
XT_FS_CALLBACK(lstat)
XT_FS_CALLBACK(statfs)
XT_FS_CALLBACK(access)
XT_FS_CALLBACK(open)
XT_FS_CALLBACK(chmod)
XT_FS_CALLBACK(lchmod)
XT_FS_CALLBACK(chown)
XT_FS_CALLBACK(lchown)
XT_FS_CALLBACK(truncate)
XT_FS_CALLBACK(utimes)
XT_FS_CALLBACK(lutimes)
XT_FS_CALLBACK(link)
XT_FS_CALLBACK(symlink)
XT_FS_CALLBACK(readlink)
XT_FS_CALLBACK(mkdtemp)
XT_FS_CALLBACK(opendir)
XT_FS_CALLBACK(glob)

/* `fs.exists(path, cb)`: `cb(boolean)`, never an error. */
xt_value xt_node_exists_cb(int32_t argc, xt_value *argv) {
  if (argc < 1 || !XT_IS_FUNCTION(argv[argc - 1])) return xt_node_exists(argc, argv);
  xt_value callback = argv[argc - 1];
  xt_value arguments[1] = {xt_node_exists(1, argv)};
  return xt_call_with_this(callback, XT_UNDEFINED, 1, arguments);
}
