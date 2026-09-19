/*
 * Node.js `fs` filesystem operations for xbintsc.
 *
 * Covers the synchronous, non-descriptor half of the module:
 *     existsSync, readdirSync, mkdirSync, rmSync, unlinkSync, rmdirSync,
 *     renameSync, copyFileSync, realpathSync, statSync, lstatSync
 *
 * Error handling is intentionally Node-compatible in spirit but not in
 * mechanics: xbintsc has no try/catch that can catch native errors yet, so
 * failures print to stderr and return `undefined` (or `false` for
 * `existsSync`) instead of throwing `ENOENT`-style exceptions.
 */

#include "rt.h"
#include "fs_common.h"

#include <errno.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct _stat xt_fs_stat_t;
#define xt_fs_stat_fn _stat
#define xt_fs_lstat_fn _stat
#define xt_fs_access _access

#ifndef F_OK
#define F_OK 0
#endif
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

/* Directory iteration backed by the CRT's _findfirst/_findnext. */
typedef struct {
  intptr_t handle;
  struct _finddata_t entry;
} xt_fs_dir;

static int xt_fs_dir_open(xt_fs_dir *dir, const char *path) {
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

static int xt_fs_dir_next(xt_fs_dir *dir) { return _findnext(dir->handle, &dir->entry) == 0; }

static const char *xt_fs_dir_name(xt_fs_dir *dir) { return dir->entry.name; }

static void xt_fs_dir_close(xt_fs_dir *dir) { _findclose(dir->handle); }
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct stat xt_fs_stat_t;
#define xt_fs_stat_fn stat
#define xt_fs_lstat_fn lstat
#define xt_fs_access access

typedef struct {
  DIR *handle;
  struct dirent *entry;
} xt_fs_dir;

static int xt_fs_dir_open(xt_fs_dir *dir, const char *path) {
  dir->handle = opendir(path);
  dir->entry = NULL;
  return dir->handle != NULL;
}

static int xt_fs_dir_next(xt_fs_dir *dir) {
  dir->entry = readdir(dir->handle);
  return dir->entry != NULL;
}

static const char *xt_fs_dir_name(xt_fs_dir *dir) { return dir->entry->d_name; }

static void xt_fs_dir_close(xt_fs_dir *dir) { closedir(dir->handle); }
#endif

#if defined(_WIN32)
#define xt_fs_mkdir_one(path) _mkdir(path)
#define xt_fs_rmdir_one(path) _rmdir(path)
#else
#define xt_fs_mkdir_one(path) mkdir(path, 0777)
#define xt_fs_rmdir_one(path) rmdir(path)
#endif

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

xt_value xt_node_read_dir(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_string_data(xt_to_string(argv[0]));
  if (!path) return xt_undefined();

  xt_fs_dir dir;
  if (!xt_fs_dir_open(&dir, path)) {
    xt_fs_error("open directory", path);
    return xt_undefined();
  }

  size_t count = 0;
  size_t capacity = 16;
  xt_value *items = (xt_value *)malloc(sizeof(xt_value) * capacity);
  if (!items) {
    xt_fs_dir_close(&dir);
    return xt_undefined();
  }

  while (xt_fs_dir_next(&dir)) {
    const char *name = xt_fs_dir_name(&dir);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    if (count == capacity) {
      capacity *= 2;
      xt_value *grown = (xt_value *)realloc(items, sizeof(xt_value) * capacity);
      if (!grown) break;
      items = grown;
    }
    items[count++] = xt_string_from_cstr(name);
  }
  xt_fs_dir_close(&dir);

  xt_value result = xt_array_new((int32_t)count, items);
  free(items);
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

static int xt_fs_mkdir_recursive(const char *path) {
  size_t length = strlen(path);
  char *copy = (char *)malloc(length + 1);
  if (!copy) return -1;
  memcpy(copy, path, length + 1);

  while (length > 1 && copy[length - 1] == '/') copy[--length] = '\0';
  for (size_t i = 1; i <= length; i++) {
    if (copy[i] != '/' && copy[i] != '\0') continue;
    char saved = copy[i];
    copy[i] = '\0';
    if (xt_fs_mkdir_one(copy) != 0 && errno != EEXIST) {
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

  if (argc > 1 && xt_fs_recursive(argv[1])) {
    if (xt_fs_mkdir_recursive(path) != 0) xt_fs_error("create directory", path);
    return xt_undefined();
  }
  if (xt_fs_mkdir_one(path) != 0 && errno != EEXIST) xt_fs_error("create directory", path);
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

static xt_value xt_node_stat_result(const xt_fs_stat_t *info) {
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
