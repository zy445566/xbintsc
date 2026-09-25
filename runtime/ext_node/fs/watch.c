/*
 * `fs.watch` / `fs.watchFile` / `fs.unwatchFile` for xbintsc.
 *
 * The runtime has no event loop or timers, so file-change notifications can
 * never fire. These builtins still return Node-shaped watcher objects (an
 * emitter with a `close()` method) so that feature-detection and cleanup code
 * keeps working; no `change`/`rename` events are delivered. This is a
 * documented deviation.
 */

#include "../node_common.h"
#include "fs_common.h"

static xt_value xt_fs_watch_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  return xt_undefined();
}

static xt_value xt_fs_watch_make(xt_value filename, const char *listenerName) {
  xt_value watcher = xt_object_new();
  xt_node_install_emitter(watcher);
  xt_node_define_method(watcher, "close", (void *)xt_fs_watch_close);
  if (XT_IS_STRING(filename)) xt_node_set(watcher, "path", filename);
  if (listenerName) xt_node_set(watcher, "kind", xt_string_from_cstr(listenerName));
  return watcher;
}

xt_value xt_node_watch(int32_t argc, xt_value *argv) {
  xt_value filename = argc > 0 ? argv[0] : XT_UNDEFINED;
  for (int32_t i = 1; i < argc; i++) {
    if (XT_IS_FUNCTION(argv[i])) {
      xt_value watcher = xt_fs_watch_make(filename, NULL);
      xt_node_listeners(watcher, "change", 1);
      return watcher;
    }
  }
  return xt_fs_watch_make(filename, NULL);
}

xt_value xt_node_watch_file(int32_t argc, xt_value *argv) {
  xt_value filename = argc > 0 ? argv[0] : XT_UNDEFINED;
  return xt_fs_watch_make(filename, "file");
}

xt_value xt_node_unwatch_file(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_undefined();
}
