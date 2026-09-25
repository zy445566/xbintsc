/*
 * Node.js `fs.cpSync` for xbintsc.
 *
 * Copies files, symbolic links and (with `recursive: true`) directories.
 * Supported options: `recursive`, `force`, `errorOnExist`, `dereference`,
 * `preserveTimestamps`. Other Node options are ignored (documented).
 */

#include "../node_common.h"
#include "fs_common.h"

#if !defined(_WIN32)
#include <sys/time.h>
#include <unistd.h>
#endif

static int xt_fs_option_bool(xt_value options, const char *key, int fallback) {
  if (!XT_IS_OBJECT(options)) return fallback;
  xt_value value = xt_object_get_cstr(options, key);
  if (XT_IS_UNDEFINED(value) || XT_IS_NULL(value)) return fallback;
  return xt_truthy(value);
}

static void xt_fs_copy_file_bytes(const char *from, const char *to) {
  FILE *source = fopen(from, "rb");
  if (!source) {
    xt_fs_error("open", from);
    return;
  }
  FILE *destination = fopen(to, "wb");
  if (!destination) {
    fclose(source);
    xt_fs_error("open", to);
    return;
  }
  char buffer[8192];
  size_t read;
  while ((read = fread(buffer, 1, sizeof(buffer), source)) > 0) fwrite(buffer, 1, read, destination);
  fclose(source);
  fclose(destination);
}

static char *xt_fs_join(const char *dir, const char *name) {
  size_t size = strlen(dir) + strlen(name) + 2;
  char *path = (char *)malloc(size);
  if (!path) return NULL;
  snprintf(path, size, "%s/%s", dir, name);
  return path;
}

static void xt_fs_copy_any(const char *src, const char *dest, xt_value options, int recursive) {
  xt_fs_stat_t info;
  if (xt_fs_lstat_fn(src, &info) != 0) {
    xt_fs_raise_path("lstat", src);
    return;
  }
  int force = xt_fs_option_bool(options, "force", 1);
  int errorOnExist = xt_fs_option_bool(options, "errorOnExist", 0);
  int dereference = xt_fs_option_bool(options, "dereference", 0);

  if (S_ISDIR(info.st_mode)) {
    if (!recursive) {
      xt_fs_raise_errno(EISDIR, "cp", src);
      return;
    }
    if (xt_fs_mkdir_mode(dest, (int)(info.st_mode & 0777)) != 0 && errno != EEXIST) {
      xt_fs_raise_path("mkdir", dest);
      return;
    }
    xt_fs_dir dir;
    if (!xt_fs_dir_open(&dir, src)) {
      xt_fs_error("open directory", src);
      return;
    }
    while (xt_fs_dir_next(&dir)) {
      const char *name = xt_fs_dir_name(&dir);
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
      char *childSrc = xt_fs_join(src, name);
      char *childDest = xt_fs_join(dest, name);
      if (childSrc && childDest) xt_fs_copy_any(childSrc, childDest, options, recursive);
      free(childSrc);
      free(childDest);
    }
    xt_fs_dir_close(&dir);
    return;
  }

  if (xt_fs_access(dest, F_OK) == 0) {
    if (!force) {
      if (errorOnExist) xt_fs_raise_errno(EEXIST, "cp", dest);
      return;
    }
  }

#if !defined(_WIN32)
  if (S_ISLNK(info.st_mode) && !dereference) {
    size_t capacity = 256;
    for (;;) {
      char *target = (char *)malloc(capacity);
      if (!target) return;
      ssize_t length = readlink(src, target, capacity);
      if (length < 0) {
        free(target);
        xt_fs_raise_path("readlink", src);
        return;
      }
      if ((size_t)length < capacity) {
        target[length] = '\0';
        if (symlink(target, dest) != 0) xt_fs_raise_path("symlink", dest);
        free(target);
        return;
      }
      free(target);
      capacity *= 2;
    }
  }
#endif

  xt_fs_copy_file_bytes(src, dest);

  if (xt_fs_option_bool(options, "preserveTimestamps", 0)) {
#if !defined(_WIN32)
    struct timeval times[2];
    times[0].tv_sec = info.st_atime;
    times[0].tv_usec = 0;
    times[1].tv_sec = info.st_mtime;
    times[1].tv_usec = 0;
    utimes(dest, times);
#endif
  }
}

xt_value xt_node_cp(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_undefined();
  const char *src = xt_node_cstr(argv[0]);
  const char *dest = xt_node_cstr(argv[1]);
  if (!src || !dest) {
    xt_fs_raise_errno(EINVAL, "cp", NULL);
    return xt_undefined();
  }
  xt_value options = argc > 2 ? argv[2] : XT_UNDEFINED;
  int recursive = xt_fs_option_bool(options, "recursive", 0);
  xt_fs_copy_any(src, dest, options, recursive);
  return xt_undefined();
}
