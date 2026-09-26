/* -- directory iteration -------------------------------------------------- */

#if defined(_WIN32)
/* Directory iteration backed by the CRT's _findfirst/_findnext. */
typedef struct {
  intptr_t handle;
  struct _finddata_t entry;
} xt_fs_dir;

static inline int xt_fs_dir_open(xt_fs_dir *dir, const char *path) {
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

static inline int xt_fs_dir_next(xt_fs_dir *dir) { return _findnext(dir->handle, &dir->entry) == 0; }
static inline const char *xt_fs_dir_name(xt_fs_dir *dir) { return dir->entry.name; }
static inline void xt_fs_dir_close(xt_fs_dir *dir) { _findclose(dir->handle); }
#else
#include <dirent.h>

typedef struct {
  DIR *handle;
  struct dirent *entry;
} xt_fs_dir;

static inline int xt_fs_dir_open(xt_fs_dir *dir, const char *path) {
  dir->handle = opendir(path);
  dir->entry = NULL;
  return dir->handle != NULL;
}

static inline int xt_fs_dir_next(xt_fs_dir *dir) {
  dir->entry = readdir(dir->handle);
  return dir->entry != NULL;
}

static inline const char *xt_fs_dir_name(xt_fs_dir *dir) { return dir->entry->d_name; }
static inline void xt_fs_dir_close(xt_fs_dir *dir) { closedir(dir->handle); }
#endif
