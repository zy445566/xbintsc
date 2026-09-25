/*
 * Node.js `fs` filesystem operations for xbintsc.
 *
 * Covers the synchronous, non-descriptor half of the module:
 *     existsSync, readdirSync, mkdirSync, rmSync, unlinkSync, rmdirSync,
 *     renameSync, copyFileSync, realpathSync, statSync, lstatSync
 *
 * Error handling follows Node: failures throw a shaped Error (see
 * `xt_fs_error`), which `fs/promises` converts into a rejected Promise.
 */

#include "rt.h"
#include "fs_common.h"

#include <errno.h>

/* ------------------------------------------------------------------------- */
/* existsSync                                                                */
/* ------------------------------------------------------------------------- */

xt_value xt_node_exists(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_bool(0);
  const char *path = xt_string_data(xt_to_string(argv[0]));
  if (!path) return xt_bool(0);
  return xt_bool(xt_fs_access(path, F_OK) == 0);
}

/* ------------------------------------------------------------------------- */
/* readdirSync                                                               */
/* ------------------------------------------------------------------------- */

/* Defined alongside `statSync` further down; the `Dirent` objects built for
   `readdirSync(path, { withFileTypes: true })` reuse the same closures. */
static void xt_fs_define_stat_method(xt_value object, const char *name, unsigned int mode);

/* Build the Node `Dirent`-shaped object (`name` plus stat predicates) for a
   single directory entry. Types come from `lstat`, matching Node's `d_type`
   based `Dirent` (a symlink reports `isSymbolicLink()` true). */
xt_value xt_node_dirent_result(const char *dir, const char *name) {
  size_t size = strlen(dir) + strlen(name) + 2;
  char *full = (char *)malloc(size);
  unsigned int mode = 0;
  if (full) {
    snprintf(full, size, "%s/%s", dir, name);
    xt_fs_stat_t info;
    if (xt_fs_lstat_fn(full, &info) == 0) mode = (unsigned int)info.st_mode;
    free(full);
  }

  xt_value object = xt_object_new();
  xt_object_set(object, xt_string_from_cstr("name"), xt_string_from_cstr(name));
  xt_fs_define_stat_method(object, "isFile", mode);
  xt_fs_define_stat_method(object, "isDirectory", mode);
  xt_fs_define_stat_method(object, "isSymbolicLink", mode);
  xt_fs_define_stat_method(object, "isFIFO", mode);
  xt_fs_define_stat_method(object, "isSocket", mode);
  xt_fs_define_stat_method(object, "isBlockDevice", mode);
  xt_fs_define_stat_method(object, "isCharacterDevice", mode);
  return object;
}

typedef struct {
  xt_value *items;
  size_t count;
  size_t capacity;
} xt_fs_list;

static void xt_fs_list_push(xt_fs_list *list, xt_value value) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 16;
    xt_value *grown = (xt_value *)realloc(list->items, sizeof(xt_value) * list->capacity);
    if (!grown) return;
    list->items = grown;
  }
  list->items[list->count++] = value;
}

/* Depth-first walk used by `readdirSync(path, { recursive: true })`. `prefix`
   is the directory path relative to `base` (empty for the root, otherwise
   ending in `/`). */
static void xt_fs_readdir_into(const char *base, const char *prefix, int withFileTypes, int recursive,
                               xt_fs_list *list) {
  size_t baseLength = strlen(base);
  size_t prefixLength = strlen(prefix);
  size_t dirSize = baseLength + prefixLength + 2;
  char *dirPath = (char *)malloc(dirSize);
  if (!dirPath) return;
  if (prefixLength == 0) snprintf(dirPath, dirSize, "%s", base);
  else snprintf(dirPath, dirSize, "%s/%s", base, prefix);
  size_t dirLength = strlen(dirPath);
  while (dirLength > 1 && dirPath[dirLength - 1] == '/') dirPath[--dirLength] = '\0';

  xt_fs_dir dir;
  if (!xt_fs_dir_open(&dir, dirPath)) {
    xt_fs_error("open directory", dirPath);
    free(dirPath);
    return;
  }
  while (xt_fs_dir_next(&dir)) {
    const char *name = xt_fs_dir_name(&dir);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    size_t nameLength = strlen(name);
    size_t relSize = prefixLength + nameLength + 1;
    char *relative = (char *)malloc(relSize);
    if (!relative) continue;
    snprintf(relative, relSize, "%s%s", prefix, name);
    xt_fs_list_push(list, withFileTypes ? xt_node_dirent_result(dirPath, name) : xt_string_from_cstr(relative));

    if (recursive) {
      size_t fullSize = dirLength + nameLength + 2;
      char *full = (char *)malloc(fullSize);
      if (full) {
        snprintf(full, fullSize, "%s/%s", dirPath, name);
        xt_fs_stat_t info;
        if (xt_fs_lstat_fn(full, &info) == 0 && S_ISDIR(info.st_mode)) {
          size_t childSize = relSize + 1;
          char *child = (char *)malloc(childSize);
          if (child) {
            snprintf(child, childSize, "%s/", relative);
            xt_fs_readdir_into(base, child, withFileTypes, recursive, list);
            free(child);
          }
        }
        free(full);
      }
    }
    free(relative);
  }
  xt_fs_dir_close(&dir);
  free(dirPath);
}

xt_value xt_node_read_dir(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_string_data(xt_to_string(argv[0]));
  if (!path) return xt_undefined();

  /* `readdirSync(path, { withFileTypes: true })` yields `Dirent` objects;
     without the option the result is a plain array of entry names. */
  xt_value options = argc > 1 ? argv[1] : XT_UNDEFINED;
  int withFileTypes = XT_IS_OBJECT(options) && xt_truthy(xt_object_get_cstr(options, "withFileTypes"));
  int recursive = XT_IS_OBJECT(options) && xt_truthy(xt_object_get_cstr(options, "recursive"));

  xt_fs_list list;
  list.items = NULL;
  list.count = 0;
  list.capacity = 0;
  xt_fs_readdir_into(path, "", withFileTypes, recursive, &list);

  xt_value result = xt_array_new((int32_t)list.count, list.items);
  free(list.items);
  return result;
}

/* ------------------------------------------------------------------------- */
/* mkdirSync                                                                 */
/* ------------------------------------------------------------------------- */

/* True when the second argument asks for recursive creation. */
static int xt_fs_recursive(xt_value options) {
  if (!xt_truthy(options)) return 0;
  if (XT_IS_OBJECT(options)) return xt_truthy(xt_object_get_cstr(options, "recursive"));
  return 1;
}

static int xt_fs_mkdir_recursive(const char *path, int mode) {
  size_t length = strlen(path);
  char *copy = (char *)malloc(length + 1);
  if (!copy) return -1;
  memcpy(copy, path, length + 1);

  while (length > 1 && copy[length - 1] == '/') copy[--length] = '\0';
  for (size_t i = 1; i <= length; i++) {
    if (copy[i] != '/' && copy[i] != '\0') continue;
    char saved = copy[i];
    copy[i] = '\0';
    if (xt_fs_mkdir_mode(copy, mode) != 0 && errno != EEXIST) {
      free(copy);
      return -1;
    }
    copy[i] = saved;
  }
  free(copy);
  return 0;
}

xt_value xt_node_mkdir(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_string_data(xt_to_string(argv[0]));
  if (!path) return xt_undefined();

  xt_value options = argc > 1 ? argv[1] : XT_UNDEFINED;
  int mode = 0777;
  if (XT_IS_OBJECT(options) && !XT_IS_UNDEFINED(xt_object_get_cstr(options, "mode"))) {
    mode = (int)xt_to_number(xt_object_get_cstr(options, "mode"));
  }

  if (argc > 1 && xt_fs_recursive(options)) {
    if (xt_fs_mkdir_recursive(path, mode) != 0) xt_fs_error("create directory", path);
    return xt_undefined();
  }
  if (xt_fs_mkdir_mode(path, mode) != 0 && errno != EEXIST) xt_fs_error("create directory", path);
  return xt_undefined();
}

/* ------------------------------------------------------------------------- */
/* rmSync / unlinkSync / rmdirSync                                           */
/* ------------------------------------------------------------------------- */

static int xt_fs_remove_recursive(const char *path) {
  xt_fs_stat_t info;
  if (xt_fs_lstat_fn(path, &info) != 0) return -1;
  if (!S_ISDIR(info.st_mode)) return remove(path);

  xt_fs_dir dir;
  if (!xt_fs_dir_open(&dir, path)) return -1;
  while (xt_fs_dir_next(&dir)) {
    const char *name = xt_fs_dir_name(&dir);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    size_t size = strlen(path) + strlen(name) + 2;
    char *child = (char *)malloc(size);
    if (!child) continue;
    snprintf(child, size, "%s/%s", path, name);
    xt_fs_remove_recursive(child);
    free(child);
  }
  xt_fs_dir_close(&dir);
  return xt_fs_rmdir_one(path);
}

xt_value xt_node_rm(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_string_data(xt_to_string(argv[0]));
  if (!path) return xt_undefined();

  int recursive = argc > 1 && xt_fs_recursive(argv[1]);
  int status = recursive ? xt_fs_remove_recursive(path) : remove(path);
  if (status != 0) xt_fs_error("remove", path);
  return xt_undefined();
}

xt_value xt_node_unlink(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_string_data(xt_to_string(argv[0]));
  if (!path) return xt_undefined();
  if (remove(path) != 0) xt_fs_error("remove file", path);
  return xt_undefined();
}

xt_value xt_node_rmdir(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_string_data(xt_to_string(argv[0]));
  if (!path) return xt_undefined();
  if (xt_fs_rmdir_one(path) != 0) xt_fs_error("remove directory", path);
  return xt_undefined();
}

/* ------------------------------------------------------------------------- */
/* renameSync / copyFileSync / realpathSync                                  */
/* ------------------------------------------------------------------------- */

xt_value xt_node_rename(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_undefined();
  const char *from = xt_string_data(xt_to_string(argv[0]));
  const char *to = xt_string_data(xt_to_string(argv[1]));
  if (!from || !to) return xt_undefined();
  if (rename(from, to) != 0) xt_fs_error("rename", from);
  return xt_undefined();
}

xt_value xt_node_copy_file(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_undefined();
  const char *from = xt_string_data(xt_to_string(argv[0]));
  const char *to = xt_string_data(xt_to_string(argv[1]));
  if (!from || !to) return xt_undefined();

  int flags = argc > 2 ? (int)xt_to_number(argv[2]) : 0;
  if ((flags & 1) && xt_fs_access(to, F_OK) == 0) {
    xt_fs_raise_errno(EEXIST, "copyfile", to);
    return xt_undefined();
  }

  FILE *source = fopen(from, "rb");
  if (!source) {
    xt_fs_error("open", from);
    return xt_undefined();
  }
  FILE *destination = fopen(to, "wb");
  if (!destination) {
    fclose(source);
    xt_fs_error("open", to);
    return xt_undefined();
  }

  char buffer[8192];
  size_t read;
  while ((read = fread(buffer, 1, sizeof(buffer), source)) > 0) fwrite(buffer, 1, read, destination);
  fclose(source);
  fclose(destination);
  return xt_undefined();
}

xt_value xt_node_realpath(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_string_data(xt_to_string(argv[0]));
  if (!path) return xt_undefined();
#if defined(_WIN32)
  char resolved[_MAX_PATH];
  if (_fullpath(resolved, path, _MAX_PATH) == NULL) {
    xt_fs_error("resolve", path);
    return xt_undefined();
  }
  return xt_string_from_cstr(resolved);
#else
  char *resolved = realpath(path, NULL);
  if (!resolved) {
    xt_fs_error("resolve", path);
    return xt_undefined();
  }
  xt_value result = xt_string_from_cstr(resolved);
  free(resolved);
  return result;
#endif
}

/* ------------------------------------------------------------------------- */
/* statSync / lstatSync                                                      */
/* ------------------------------------------------------------------------- */

/*
 * Stats methods (`isFile`, `isDirectory`, ...) are native closures capturing
 * the raw mode; the closure environment carries `[mode, methodName]`.
 */
static xt_value xt_fs_stat_method(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  unsigned int mode = (unsigned int)xt_to_number(xt_closure_env(env, 0));
  const char *method = xt_string_data(xt_closure_env(env, 1));
  if (!method) return xt_bool(0);
  int result = 0;
  if (strcmp(method, "isFile") == 0) result = S_ISREG(mode);
  else if (strcmp(method, "isDirectory") == 0) result = S_ISDIR(mode);
  else if (strcmp(method, "isSymbolicLink") == 0) result = S_ISLNK(mode);
  else if (strcmp(method, "isFIFO") == 0) result = S_ISFIFO(mode);
  else if (strcmp(method, "isSocket") == 0) result = S_ISSOCK(mode);
  else if (strcmp(method, "isBlockDevice") == 0) result = S_ISBLK(mode);
  else if (strcmp(method, "isCharacterDevice") == 0) result = S_ISCHR(mode);
  return xt_bool(result);
}

static void xt_fs_define_stat_method(xt_value object, const char *name, unsigned int mode) {
  xt_value env[2];
  env[0] = xt_number((double)mode);
  env[1] = xt_string_from_cstr(name);
  xt_value fn = xt_closure_new((void *)xt_fs_stat_method, 2, env);
  xt_object_set(object, xt_string_from_cstr(name), fn);
}

static double xt_fs_seconds_ms(long seconds, long nanoseconds) {
  return (double)seconds * 1000.0 + (double)nanoseconds / 1000000.0;
}

xt_value xt_node_stat_result(const xt_fs_stat_t *info) {
  xt_value object = xt_object_new();
  xt_object_set(object, xt_string_from_cstr("size"), xt_number((double)info->st_size));
  xt_object_set(object, xt_string_from_cstr("mode"), xt_number((double)info->st_mode));
  xt_object_set(object, xt_string_from_cstr("uid"), xt_number((double)info->st_uid));
  xt_object_set(object, xt_string_from_cstr("gid"), xt_number((double)info->st_gid));
  xt_object_set(object, xt_string_from_cstr("dev"), xt_number((double)info->st_dev));
  xt_object_set(object, xt_string_from_cstr("ino"), xt_number((double)info->st_ino));
  xt_object_set(object, xt_string_from_cstr("nlink"), xt_number((double)info->st_nlink));
  xt_object_set(object, xt_string_from_cstr("rdev"), xt_number((double)info->st_rdev));
#if defined(__APPLE__)
  xt_object_set(object, xt_string_from_cstr("blocks"), xt_number((double)info->st_blocks));
  xt_object_set(object, xt_string_from_cstr("blksize"), xt_number((double)info->st_blksize));
  xt_object_set(object, xt_string_from_cstr("mtimeMs"),
                xt_number(xt_fs_seconds_ms(info->st_mtimespec.tv_sec, info->st_mtimespec.tv_nsec)));
  xt_object_set(object, xt_string_from_cstr("atimeMs"),
                xt_number(xt_fs_seconds_ms(info->st_atimespec.tv_sec, info->st_atimespec.tv_nsec)));
  xt_object_set(object, xt_string_from_cstr("ctimeMs"),
                xt_number(xt_fs_seconds_ms(info->st_ctimespec.tv_sec, info->st_ctimespec.tv_nsec)));
#elif defined(_WIN32)
  xt_object_set(object, xt_string_from_cstr("mtimeMs"), xt_number((double)info->st_mtime * 1000.0));
  xt_object_set(object, xt_string_from_cstr("atimeMs"), xt_number((double)info->st_atime * 1000.0));
  xt_object_set(object, xt_string_from_cstr("ctimeMs"), xt_number((double)info->st_ctime * 1000.0));
#else
  xt_object_set(object, xt_string_from_cstr("blocks"), xt_number((double)info->st_blocks));
  xt_object_set(object, xt_string_from_cstr("blksize"), xt_number((double)info->st_blksize));
  xt_object_set(object, xt_string_from_cstr("mtimeMs"),
                xt_number(xt_fs_seconds_ms(info->st_mtim.tv_sec, info->st_mtim.tv_nsec)));
  xt_object_set(object, xt_string_from_cstr("atimeMs"),
                xt_number(xt_fs_seconds_ms(info->st_atim.tv_sec, info->st_atim.tv_nsec)));
  xt_object_set(object, xt_string_from_cstr("ctimeMs"),
                xt_number(xt_fs_seconds_ms(info->st_ctim.tv_sec, info->st_ctim.tv_nsec)));
#endif

  unsigned int mode = (unsigned int)info->st_mode;
  xt_fs_define_stat_method(object, "isFile", mode);
  xt_fs_define_stat_method(object, "isDirectory", mode);
  xt_fs_define_stat_method(object, "isSymbolicLink", mode);
  xt_fs_define_stat_method(object, "isFIFO", mode);
  xt_fs_define_stat_method(object, "isSocket", mode);
  xt_fs_define_stat_method(object, "isBlockDevice", mode);
  xt_fs_define_stat_method(object, "isCharacterDevice", mode);
  return object;
}

static xt_value xt_node_stat_common(int32_t argc, xt_value *argv, int followLinks) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_string_data(xt_to_string(argv[0]));
  if (!path) return xt_undefined();

  xt_fs_stat_t info;
  int status = followLinks ? xt_fs_stat_fn(path, &info) : xt_fs_lstat_fn(path, &info);
  if (status != 0) {
    xt_fs_error("stat", path);
    return xt_undefined();
  }
  return xt_node_stat_result(&info);
}

xt_value xt_node_stat(int32_t argc, xt_value *argv) { return xt_node_stat_common(argc, argv, 1); }

xt_value xt_node_lstat(int32_t argc, xt_value *argv) { return xt_node_stat_common(argc, argv, 0); }
