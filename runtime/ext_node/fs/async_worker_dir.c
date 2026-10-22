/*
 * Directory-walking worker jobs: `readdir`, `opendir`, recursive `rm`/`cp` and
 * `glob`. All of it runs off the main thread; only the entry table built here
 * crosses back (via async_complete.c).
 */

#include "async_worker_dir.h"

#include <errno.h>

#if !defined(_WIN32)
#include <sys/time.h>
#include <unistd.h>
#endif

/* -- readdir / opendir ----------------------------------------------------- */

static int xt_fs_worker_scan(xt_fs_job *job, const char *base, const char *prefix, int recursive, int with_types) {
  size_t baseLength = strlen(base);
  size_t prefixLength = strlen(prefix);
  char *dirPath = (char *)malloc(baseLength + prefixLength + 2);
  if (!dirPath) return -1;
  if (prefixLength == 0) snprintf(dirPath, baseLength + prefixLength + 2, "%s", base);
  else snprintf(dirPath, baseLength + prefixLength + 2, "%s/%s", base, prefix);
  size_t dirLength = strlen(dirPath);
  while (dirLength > 1 && (dirPath[dirLength - 1] == '/' || dirPath[dirLength - 1] == '\\')) {
    dirPath[--dirLength] = '\0';
  }

  xt_fs_dir dir;
  if (!xt_fs_dir_open(&dir, dirPath)) {
    int saved = errno;
    free(dirPath);
    errno = saved;
    return -1;
  }
  while (xt_fs_dir_next(&dir)) {
    const char *name = xt_fs_dir_name(&dir);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    size_t nameLength = strlen(name);
    char *relative = (char *)malloc(prefixLength + nameLength + 1);
    if (!relative) continue;
    snprintf(relative, prefixLength + nameLength + 1, "%s%s", prefix, name);
    unsigned int mode = 0;
    if (recursive || with_types) {
      char *full = xt_fs_worker_join(dirPath, name);
      if (full) {
        xt_fs_stat_t info;
        if (xt_fs_lstat_fn(full, &info) == 0) mode = (unsigned int)info.st_mode;
        free(full);
      }
    }
    if (xt_fs_entry_push(job, relative, name, mode) != 0) {
      free(relative);
      break;
    }
    if (recursive && mode && S_ISDIR(mode)) {
      char *child = (char *)malloc(prefixLength + nameLength + 2);
      if (child) {
        snprintf(child, prefixLength + nameLength + 2, "%s%s/", prefix, name);
        xt_fs_worker_scan(job, base, child, recursive, with_types);
        free(child);
      }
    }
    free(relative);
  }
  xt_fs_dir_close(&dir);
  free(dirPath);
  return 0;
}

void xt_fs_worker_readdir(xt_fs_job *job) {
  if (!job->path) {
    xt_fs_job_fail(job, EINVAL, "readdir", NULL);
    return;
  }
  if (xt_fs_worker_scan(job, job->path, "", job->recursive, job->with_types) != 0) {
    xt_fs_job_fail(job, errno, "open directory", job->path);
  }
}

void xt_fs_worker_opendir(xt_fs_job *job) {
  if (!job->path) {
    xt_fs_job_fail(job, EINVAL, "opendir", NULL);
    return;
  }
  if (xt_fs_worker_scan(job, job->path, "", 0, 1) != 0) {
    xt_fs_job_fail(job, errno, "open directory", job->path);
  }
}

/* -- recursive rm ---------------------------------------------------------- */

static int xt_fs_worker_remove(const char *path) {
  xt_fs_stat_t info;
  if (xt_fs_lstat_fn(path, &info) != 0) return -1;
  if (!S_ISDIR(info.st_mode)) {
#if defined(_WIN32)
    return _unlink(path);
#else
    return unlink(path);
#endif
  }
  xt_fs_dir dir;
  if (!xt_fs_dir_open(&dir, path)) return -1;
  while (xt_fs_dir_next(&dir)) {
    const char *name = xt_fs_dir_name(&dir);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    char *child = xt_fs_worker_join(path, name);
    if (!child) continue;
    xt_fs_worker_remove(child);
    free(child);
  }
  xt_fs_dir_close(&dir);
  return xt_fs_rmdir_one(path);
}

void xt_fs_worker_remove_tree(xt_fs_job *job) {
  if (!job->path) {
    xt_fs_job_fail(job, EINVAL, "remove", NULL);
    return;
  }
  int status = job->recursive ? xt_fs_worker_remove(job->path) : remove(job->path);
  if (status != 0) {
    if (job->force && errno == ENOENT) return;
    xt_fs_job_fail(job, errno, "remove", job->path);
  }
}

/* Stream `from` into `to` without materialising the whole file. */
int xt_fs_worker_copy_bytes(const char *from, const char *to) {
  int source = XT_FS_OPEN(from, XT_FS_RDONLY | XT_FS_BINARY_FLAG, 0);
  if (source < 0) return -1;
  int destination = XT_FS_OPEN(to, XT_FS_WRONLY | XT_FS_CREAT | XT_FS_TRUNC | XT_FS_BINARY_FLAG, 0666);
  if (destination < 0) {
    int saved = errno;
    XT_FS_CLOSE(source);
    errno = saved;
    return -1;
  }
  char buffer[65536];
  int status = 0;
  for (;;) {
    ssize_t count = XT_FS_READ(source, buffer, sizeof(buffer));
    if (count < 0) {
      status = -1;
      break;
    }
    if (count == 0) break;
    size_t written = 0;
    while (written < (size_t)count) {
      ssize_t chunk = XT_FS_WRITE(destination, buffer + written, (size_t)count - written);
      if (chunk <= 0) {
        status = -1;
        break;
      }
      written += (size_t)chunk;
    }
    if (status != 0) break;
  }
  int saved = errno;
  if (XT_FS_CLOSE(source) != 0 && status == 0) {
    status = -1;
    saved = errno;
  }
  if (XT_FS_CLOSE(destination) != 0 && status == 0) {
    status = -1;
    saved = errno;
  }
  errno = saved;
  return status;
}

void xt_fs_worker_copy_file(xt_fs_job *job) {
  if (!job->path || !job->path2) {
    xt_fs_job_fail(job, EINVAL, "copyfile", NULL);
    return;
  }
  if ((job->flags & 1) && xt_fs_access(job->path2, F_OK) == 0) {
    xt_fs_job_fail(job, EEXIST, "copyfile", job->path2);
    return;
  }
  if (xt_fs_worker_copy_bytes(job->path, job->path2) != 0) {
    xt_fs_job_fail(job, errno, "open", job->path);
  }
}

/* -- recursive cp ---------------------------------------------------------- */

static int xt_fs_worker_copy_any(const char *src, const char *dest, xt_fs_job *job) {
  xt_fs_stat_t info;
  if (xt_fs_lstat_fn(src, &info) != 0) return -1;
  if (S_ISDIR(info.st_mode)) {
    if (!job->recursive) {
      errno = EISDIR;
      return -1;
    }
    if (xt_fs_mkdir_mode(dest, (int)(info.st_mode & 0777)) != 0 && errno != EEXIST) return -1;
    xt_fs_dir dir;
    if (!xt_fs_dir_open(&dir, src)) return -1;
    int status = 0;
    while (xt_fs_dir_next(&dir)) {
      const char *name = xt_fs_dir_name(&dir);
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
      char *childSrc = xt_fs_worker_join(src, name);
      char *childDest = xt_fs_worker_join(dest, name);
      if (childSrc && childDest && xt_fs_worker_copy_any(childSrc, childDest, job) != 0) status = -1;
      free(childSrc);
      free(childDest);
    }
    xt_fs_dir_close(&dir);
    return status;
  }

  if (xt_fs_access(dest, F_OK) == 0) {
    if (!job->force) {
      if (job->error_on_exist) {
        errno = EEXIST;
        return -1;
      }
      return 0;
    }
  }

#if !defined(_WIN32)
  if (S_ISLNK(info.st_mode) && !job->dereference) {
    size_t capacity = 256;
    for (;;) {
      char *target = (char *)malloc(capacity);
      if (!target) return -1;
      ssize_t length = readlink(src, target, capacity);
      if (length < 0) {
        free(target);
        return -1;
      }
      if ((size_t)length < capacity) {
        target[length] = '\0';
        int status = symlink(target, dest);
        free(target);
        return status;
      }
      free(target);
      capacity *= 2;
    }
  }
#endif

  int status = xt_fs_worker_copy_bytes(src, dest);
  if (status != 0) return -1;

  if (job->preserve) {
#if !defined(_WIN32)
    struct timeval times[2];
    times[0].tv_sec = info.st_atime;
    times[0].tv_usec = 0;
    times[1].tv_sec = info.st_mtime;
    times[1].tv_usec = 0;
    utimes(dest, times);
#endif
  }
  return 0;
}

void xt_fs_worker_copy_tree(xt_fs_job *job) {
  if (!job->path || !job->path2) {
    xt_fs_job_fail(job, EINVAL, "cp", NULL);
    return;
  }
  if (xt_fs_worker_copy_any(job->path, job->path2, job) != 0) {
    xt_fs_job_fail(job, errno, "cp", job->path);
  }
}

/* -- glob ------------------------------------------------------------------ */

/* Single path-segment match (`*`, `?`, `[...]`; no separator). */
static int xt_fs_worker_glob_match(const char *pattern, const char *text) {
  while (*pattern) {
    if (*pattern == '*') {
      pattern++;
      if (!*pattern) return 1;
      while (*text) {
        if (xt_fs_worker_glob_match(pattern, text)) return 1;
        text++;
      }
      return 0;
    }
    if (*pattern == '?') {
      if (!*text) return 0;
      pattern++;
      text++;
      continue;
    }
    if (*pattern == '[') {
      const char *cursor = pattern + 1;
      int negate = 0;
      int matched = 0;
      char value = *text;
      if (*cursor == '!' || *cursor == '^') {
        negate = 1;
        cursor++;
      }
      while (*cursor && *cursor != ']') {
        char low = *cursor++;
        char high = low;
        if (*cursor == '-' && cursor[1] && cursor[1] != ']') {
          cursor++;
          high = *cursor++;
        }
        if (value >= low && value <= high) matched = 1;
      }
      if (*cursor == ']') cursor++;
      if (!*text || matched == negate) return 0;
      pattern = cursor;
      text++;
      continue;
    }
    if (*pattern != *text) return 0;
    pattern++;
    text++;
  }
  return *text == '\0';
}

static int xt_fs_worker_glob_is_dir(const char *path, int follow) {
  xt_fs_stat_t info;
  int failed = follow ? xt_fs_stat_fn(path, &info) : xt_fs_lstat_fn(path, &info);
  if (failed != 0) return 0;
  return S_ISDIR(info.st_mode);
}

static void xt_fs_worker_glob_walk(xt_fs_job *job, const char *dirPath, const char *pattern,
                                   const char *resultPrefix);

static void xt_fs_worker_glob_descend(xt_fs_job *job, const char *dirPath, const char *pattern,
                                      const char *resultPrefix) {
  xt_fs_dir dir;
  if (!xt_fs_dir_open(&dir, dirPath)) return;
  while (xt_fs_dir_next(&dir)) {
    const char *name = xt_fs_dir_name(&dir);
    if (name[0] == '.') continue;
    char *child = xt_fs_worker_join(dirPath, name);
    if (!child) continue;
    if (xt_fs_worker_glob_is_dir(child, 0)) {
      size_t size = strlen(resultPrefix) + strlen(name) + 2;
      char *childPrefix = (char *)malloc(size);
      if (childPrefix) {
        snprintf(childPrefix, size, "%s%s/", resultPrefix, name);
        xt_fs_worker_glob_walk(job, child, pattern, childPrefix);
        free(childPrefix);
      }
    }
    free(child);
  }
  xt_fs_dir_close(&dir);
}

static void xt_fs_worker_glob_walk(xt_fs_job *job, const char *dirPath, const char *pattern,
                                   const char *resultPrefix) {
  if (pattern[0] == '\0') {
    if (resultPrefix[0] != '\0') {
      size_t length = strlen(resultPrefix);
      char *trimmed = (char *)malloc(length + 1);
      if (!trimmed) return;
      memcpy(trimmed, resultPrefix, length + 1);
      while (length > 1 && trimmed[length - 1] == '/') trimmed[--length] = '\0';
      xt_fs_entry_push(job, trimmed, trimmed, 0);
      free(trimmed);
    }
    return;
  }

  const char *slash = strchr(pattern, '/');
  size_t segmentLength = slash ? (size_t)(slash - pattern) : strlen(pattern);
  const char *rest = slash ? slash + 1 : "";

  if (segmentLength == 2 && pattern[0] == '*' && pattern[1] == '*') {
    xt_fs_worker_glob_walk(job, dirPath, rest, resultPrefix);
    xt_fs_worker_glob_descend(job, dirPath, pattern, resultPrefix);
    return;
  }

  char segment[256];
  if (segmentLength >= sizeof(segment)) return;
  memcpy(segment, pattern, segmentLength);
  segment[segmentLength] = '\0';

  xt_fs_dir dir;
  if (!xt_fs_dir_open(&dir, dirPath)) return;
  while (xt_fs_dir_next(&dir)) {
    const char *name = xt_fs_dir_name(&dir);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    if (name[0] == '.' && segment[0] != '.') continue;
    if (!xt_fs_worker_glob_match(segment, name)) continue;
    char *child = xt_fs_worker_join(dirPath, name);
    if (!child) continue;
    if (rest[0] == '\0') {
      size_t size = strlen(resultPrefix) + strlen(name) + 1;
      char *result = (char *)malloc(size);
      if (result) {
        snprintf(result, size, "%s%s", resultPrefix, name);
        xt_fs_entry_push(job, result, result, 0);
        free(result);
      }
    } else if (xt_fs_worker_glob_is_dir(child, 1)) {
      size_t size = strlen(resultPrefix) + strlen(name) + 2;
      char *childPrefix = (char *)malloc(size);
      if (childPrefix) {
        snprintf(childPrefix, size, "%s%s/", resultPrefix, name);
        xt_fs_worker_glob_walk(job, child, rest, childPrefix);
        free(childPrefix);
      }
    }
    free(child);
  }
  xt_fs_dir_close(&dir);
}

static void xt_fs_worker_glob_pattern(xt_fs_job *job, const char *pattern, const char *cwd) {
  if (!pattern) return;
  const char *base = cwd && cwd[0] ? cwd : ".";
  const char *prefix = "";
  if (pattern[0] == '/') {
    base = "/";
    prefix = "/";
    pattern++;
  }
  xt_fs_worker_glob_walk(job, base, pattern, prefix);
}

void xt_fs_worker_glob(xt_fs_job *job) {
  if (job->patterns) {
    for (size_t i = 0; i < job->pattern_count; i++) {
      xt_fs_worker_glob_pattern(job, job->patterns[i], job->path2);
    }
    return;
  }
  xt_fs_worker_glob_pattern(job, job->text, job->path2);
}
