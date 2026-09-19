/*
 * Node.js `fs/promises` for xbintsc.
 *
 * xbintsc has no asynchronous I/O scheduler, so these builtins reuse the
 * synchronous `fs` implementations and wrap the result in an immediately
 * settled promise. `await`/`.then` therefore work exactly as expected while
 * the actual work happens synchronously.
 */

#include "../node_common.h"

#include <errno.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#ifndef F_OK
#define F_OK 0
#endif
#define xt_node_access _access
#else
#include <unistd.h>
#define xt_node_access access
#endif

/* Synchronous implementations provided by the sibling `fs` translation units. */
extern xt_value xt_node_read_text_file(int32_t argc, xt_value *argv);
extern xt_value xt_node_write_file(int32_t argc, xt_value *argv);
extern xt_value xt_node_append_file(int32_t argc, xt_value *argv);
extern xt_value xt_node_mkdir(int32_t argc, xt_value *argv);
extern xt_value xt_node_read_dir(int32_t argc, xt_value *argv);
extern xt_value xt_node_rm(int32_t argc, xt_value *argv);
extern xt_value xt_node_unlink(int32_t argc, xt_value *argv);
extern xt_value xt_node_rmdir(int32_t argc, xt_value *argv);
extern xt_value xt_node_rename(int32_t argc, xt_value *argv);
extern xt_value xt_node_copy_file(int32_t argc, xt_value *argv);
extern xt_value xt_node_realpath(int32_t argc, xt_value *argv);
extern xt_value xt_node_stat(int32_t argc, xt_value *argv);
extern xt_value xt_node_lstat(int32_t argc, xt_value *argv);

static xt_value xt_node_promises_wrap(int32_t argc, xt_value *argv, xt_value (*fn)(int32_t, xt_value *)) {
  return xt_promise_resolve(fn(argc, argv));
}

xt_value xt_node_p_read_file(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_read_text_file); }
xt_value xt_node_p_write_file(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_write_file); }
xt_value xt_node_p_append_file(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_append_file); }
xt_value xt_node_p_mkdir(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_mkdir); }
xt_value xt_node_p_readdir(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_read_dir); }
xt_value xt_node_p_rm(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_rm); }
xt_value xt_node_p_unlink(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_unlink); }
xt_value xt_node_p_rmdir(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_rmdir); }
xt_value xt_node_p_rename(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_rename); }
xt_value xt_node_p_copy_file(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_copy_file); }
xt_value xt_node_p_realpath(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_realpath); }
xt_value xt_node_p_stat(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_stat); }
xt_value xt_node_p_lstat(int32_t argc, xt_value *argv) { return xt_node_promises_wrap(argc, argv, xt_node_lstat); }

xt_value xt_node_p_access(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(xt_undefined());
  const char *path = xt_string_data(xt_to_string(argv[0]));
  if (!path) return xt_promise_resolve(xt_undefined());
  if (xt_node_access(path, F_OK) == 0) return xt_promise_resolve(xt_undefined());
  xt_value error = xt_object_new();
  xt_node_set(error, "code", xt_string_from_cstr("ENOENT"));
  xt_node_set(error, "errno", xt_number(-2));
  xt_node_set(error, "path", xt_string_from_cstr(path));
  xt_node_set(error, "message", xt_string_from_cstr("ENOENT: no such file or directory"));
  return xt_promise_reject(error);
}
