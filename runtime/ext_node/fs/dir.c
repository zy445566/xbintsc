/*
 * `fs.opendirSync` / `fs.promises.opendir` and the `Dir` object for xbintsc.
 *
 * A `Dir` either owns a live directory handle (the synchronous `opendirSync`
 * path) or an entry table collected by an async worker (`opendir` from
 * `fs/promises`). `readSync`/`closeSync` serve both; `read`/`close` invoke their
 * callbacks immediately. `Dir` is a plain object, so `dir instanceof Dir` is not
 * supported yet.
 */

#include "../node_common.h"
#include "fs_common.h"

/* A pointer is stored as a number; `undefined`/`NaN` must read as "none" (a
 * bare cast would turn NaN into a bogus non-zero handle on some targets). */
static void *xt_fs_dir_pointer(xt_value thisValue, const char *key) {
  double value = xt_to_number(xt_object_get_cstr(thisValue, key));
  if (!(value > 0)) return NULL;
  return (void *)(uintptr_t)value;
}

static xt_fs_dir *xt_fs_dir_handle(xt_value thisValue) { return (xt_fs_dir *)xt_fs_dir_pointer(thisValue, "__xt_dir"); }

static xt_fs_entry *xt_fs_dir_entries(xt_value thisValue) {
  return (xt_fs_entry *)xt_fs_dir_pointer(thisValue, "__xt_entries");
}

static xt_value xt_fs_dir_read_entry(xt_value thisValue) {
  xt_fs_entry *entries = xt_fs_dir_entries(thisValue);
  if (entries) {
    size_t index = (size_t)xt_to_number(xt_object_get_cstr(thisValue, "__xt_index"));
    size_t count = (size_t)xt_to_number(xt_object_get_cstr(thisValue, "__xt_count"));
    if (index >= count) return xt_null();
    xt_fs_entry *entry = &entries[index];
    xt_object_set(thisValue, xt_string_from_cstr("__xt_index"), xt_number((double)(index + 1)));
    return xt_node_dirent_named(entry->bare ? entry->bare : entry->name, entry->mode);
  }
  xt_fs_dir *dir = xt_fs_dir_handle(thisValue);
  if (!dir) return xt_null();
  const char *path = xt_node_cstr(xt_object_get_cstr(thisValue, "path"));
  while (xt_fs_dir_next(dir)) {
    const char *name = xt_fs_dir_name(dir);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    return xt_node_dirent_result(path ? path : "", name);
  }
  return xt_null();
}

static xt_value xt_fs_dir_read_sync(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_fs_dir_read_entry(thisValue);
}

static xt_value xt_fs_dir_close_sync(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_fs_entry *entries = xt_fs_dir_entries(thisValue);
  if (entries) {
    size_t count = (size_t)xt_to_number(xt_object_get_cstr(thisValue, "__xt_count"));
    for (size_t i = 0; i < count; i++) {
      free(entries[i].name);
      if (entries[i].bare != entries[i].name) free(entries[i].bare);
    }
    free(entries);
    xt_object_set(thisValue, xt_string_from_cstr("__xt_entries"), xt_number(0));
  }
  xt_fs_dir *dir = xt_fs_dir_handle(thisValue);
  if (dir) {
    xt_fs_dir_close(dir);
    free(dir);
    xt_object_set(thisValue, xt_string_from_cstr("__xt_dir"), xt_number(0));
  }
  return xt_undefined();
}

static xt_value xt_fs_dir_read_async(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg(argc, argv, 0);
  xt_value dirent = xt_fs_dir_read_entry(thisValue);
  if (XT_IS_FUNCTION(callback)) {
    xt_value args[2];
    args[0] = xt_null();
    args[1] = dirent;
    xt_call_with_this(callback, xt_undefined(), 2, args);
  }
  return xt_undefined();
}

static xt_value xt_fs_dir_close_async(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg(argc, argv, 0);
  xt_fs_dir_close_sync(thisValue, env, 0, NULL);
  if (XT_IS_FUNCTION(callback)) {
    xt_value args[1];
    args[0] = xt_null();
    xt_call_with_this(callback, xt_undefined(), 1, args);
  }
  return xt_undefined();
}

static xt_value xt_node_dir_object(const char *path) {
  xt_value object = xt_object_new();
  xt_node_set(object, "path", xt_string_from_cstr(path ? path : ""));
  xt_node_set(object, "__xt_dir", xt_number(0));
  xt_node_set(object, "__xt_entries", xt_number(0));
  xt_node_define_method(object, "readSync", (void *)xt_fs_dir_read_sync);
  xt_node_define_method(object, "closeSync", (void *)xt_fs_dir_close_sync);
  xt_node_define_method(object, "read", (void *)xt_fs_dir_read_async);
  xt_node_define_method(object, "close", (void *)xt_fs_dir_close_async);
  return object;
}

/* Adopt an entry table collected on a worker. The `Dir` owns the table and
 * frees it on close; the caller must not free it afterwards. */
xt_value xt_node_dir_from_entries(const char *path, xt_fs_entry *entries, size_t count) {
  xt_value object = xt_node_dir_object(path);
  xt_node_set(object, "__xt_entries", xt_number((double)(uintptr_t)entries));
  xt_node_set(object, "__xt_count", xt_number((double)count));
  xt_node_set(object, "__xt_index", xt_number(0));
  return object;
}

xt_value xt_node_opendir(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_node_cstr(argv[0]);
  if (!path) {
    xt_fs_raise_errno(EINVAL, "opendir", NULL);
    return xt_undefined();
  }
  xt_fs_dir *dir = (xt_fs_dir *)malloc(sizeof(xt_fs_dir));
  if (!dir) {
    xt_fs_raise_errno(ENOMEM, "opendir", path);
    return xt_undefined();
  }
  if (!xt_fs_dir_open(dir, path)) {
    free(dir);
    xt_fs_error("open directory", path);
    return xt_undefined();
  }
  xt_value object = xt_node_dir_object(path);
  xt_node_set(object, "__xt_dir", xt_number((double)(uintptr_t)dir));
  return object;
}
