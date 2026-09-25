/*
 * `fs.globSync` for xbintsc.
 *
 * A small, dependency-free globber supporting `*`, `?`, `[...]` character
 * classes and the recursive `**` segment. Supported options: `cwd` and
 * `withFileTypes`. `exclude` (a callback) and `follow` (symlinked directories)
 * are not implemented (documented deviation).
 */

#include "../node_common.h"
#include "fs_common.h"

typedef struct {
  xt_value *items;
  size_t count;
  size_t capacity;
} xt_glob_list;

static void xt_glob_push(xt_glob_list *list, xt_value value) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 16;
    xt_value *grown = (xt_value *)realloc(list->items, sizeof(xt_value) * list->capacity);
    if (!grown) return;
    list->items = grown;
  }
  list->items[list->count++] = value;
}

static char *xt_glob_join(const char *dir, const char *name) {
  size_t dirLength = strlen(dir);
  int needsSlash = dirLength > 0 && dir[dirLength - 1] != '/';
  size_t size = dirLength + (needsSlash ? 1 : 0) + strlen(name) + 1;
  char *path = (char *)malloc(size);
  if (!path) return NULL;
  snprintf(path, size, "%s%s%s", dir, needsSlash ? "/" : "", name);
  return path;
}

/* Single path-segment glob match (`*`, `?`, `[...]`; no `/`). */
static int xt_glob_match_segment(const char *pattern, const char *text) {
  while (*pattern) {
    if (*pattern == '*') {
      pattern++;
      if (!*pattern) return 1;
      while (*text) {
        if (xt_glob_match_segment(pattern, text)) return 1;
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

/* `follow` uses `stat` so a symlinked directory is traversed (needed for
 * literal path segments such as macOS's `/tmp`); `**` uses `lstat` to match
 * Node's default `follow: false` and avoid symlink cycles. */
static int xt_glob_is_dir(const char *path, int follow) {
  xt_fs_stat_t info;
  int failed = follow ? xt_fs_stat_fn(path, &info) : xt_fs_lstat_fn(path, &info);
  if (failed != 0) return 0;
  return S_ISDIR(info.st_mode);
}

static void xt_glob_walk(const char *dirPath, const char *pattern, const char *resultPrefix, int withFileTypes,
                         xt_glob_list *list);

/* Recurse one directory level for the `**` segment. */
static void xt_glob_descend(const char *dirPath, const char *pattern, const char *resultPrefix, int withFileTypes,
                            xt_glob_list *list) {
  xt_fs_dir dir;
  if (!xt_fs_dir_open(&dir, dirPath)) return;
  while (xt_fs_dir_next(&dir)) {
    const char *name = xt_fs_dir_name(&dir);
    if (name[0] == '.') continue; /* dotfiles are not matched by default */
    char *child = xt_glob_join(dirPath, name);
    if (!child) continue;
    if (xt_glob_is_dir(child, 0)) {
      size_t size = strlen(resultPrefix) + strlen(name) + 2;
      char *childPrefix = (char *)malloc(size);
      if (childPrefix) {
        snprintf(childPrefix, size, "%s%s/", resultPrefix, name);
        xt_glob_walk(child, pattern, childPrefix, withFileTypes, list);
        free(childPrefix);
      }
    }
    free(child);
  }
  xt_fs_dir_close(&dir);
}

static void xt_glob_walk(const char *dirPath, const char *pattern, const char *resultPrefix, int withFileTypes,
                         xt_glob_list *list) {
  if (pattern[0] == '\0') {
    if (resultPrefix[0] != '\0') {
      size_t length = strlen(resultPrefix);
      char *trimmed = (char *)malloc(length + 1);
      if (!trimmed) return;
      memcpy(trimmed, resultPrefix, length + 1);
      while (length > 1 && trimmed[length - 1] == '/') trimmed[--length] = '\0';
      xt_glob_push(list, xt_string_from_cstr(trimmed));
      free(trimmed);
    }
    return;
  }

  const char *slash = strchr(pattern, '/');
  size_t segmentLength = slash ? (size_t)(slash - pattern) : strlen(pattern);
  const char *rest = slash ? slash + 1 : "";

  if (segmentLength == 2 && pattern[0] == '*' && pattern[1] == '*') {
    /* Zero directories: match the remainder here. */
    xt_glob_walk(dirPath, rest, resultPrefix, withFileTypes, list);
    /* One or more: descend and retry `**`. */
    xt_glob_descend(dirPath, pattern, resultPrefix, withFileTypes, list);
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
    if (!xt_glob_match_segment(segment, name)) continue;

    char *child = xt_glob_join(dirPath, name);
    if (!child) continue;
    if (rest[0] == '\0') {
      if (withFileTypes) xt_glob_push(list, xt_node_dirent_result(dirPath, name));
      else {
        size_t size = strlen(resultPrefix) + strlen(name) + 1;
        char *result = (char *)malloc(size);
        if (result) {
          snprintf(result, size, "%s%s", resultPrefix, name);
          xt_glob_push(list, xt_string_from_cstr(result));
          free(result);
        }
      }
    } else if (xt_glob_is_dir(child, 1)) {
      size_t size = strlen(resultPrefix) + strlen(name) + 2;
      char *childPrefix = (char *)malloc(size);
      if (childPrefix) {
        snprintf(childPrefix, size, "%s%s/", resultPrefix, name);
        xt_glob_walk(child, rest, childPrefix, withFileTypes, list);
        free(childPrefix);
      }
    }
    free(child);
  }
  xt_fs_dir_close(&dir);
}

static void xt_glob_run(const char *pattern, const char *cwd, int withFileTypes, xt_glob_list *list) {
  if (!pattern) return;
  const char *base = cwd && cwd[0] ? cwd : ".";
  const char *prefix = "";
  if (pattern[0] == '/') {
    base = "/";
    prefix = "/";
    pattern++;
  }
  xt_glob_walk(base, pattern, prefix, withFileTypes, list);
}

xt_value xt_node_glob(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_array_new(0, NULL);
  xt_value options = argc > 1 ? argv[1] : XT_UNDEFINED;
  int withFileTypes = XT_IS_OBJECT(options) && xt_truthy(xt_object_get_cstr(options, "withFileTypes"));
  const char *cwd = NULL;
  xt_value cwdValue = XT_IS_OBJECT(options) ? xt_object_get_cstr(options, "cwd") : XT_UNDEFINED;
  if (XT_IS_STRING(cwdValue)) cwd = xt_string_data(cwdValue);

  xt_glob_list list;
  list.items = NULL;
  list.count = 0;
  list.capacity = 0;

  xt_value patternValue = argv[0];
  if (XT_IS_ARRAY(patternValue)) {
    int32_t count = (int32_t)xt_to_number(xt_array_length(patternValue));
    for (int32_t i = 0; i < count; i++) {
      xt_value item = xt_array_get(patternValue, xt_number((double)i));
      if (XT_IS_STRING(item)) xt_glob_run(xt_string_data(item), cwd, withFileTypes, &list);
    }
  } else {
    const char *pattern = xt_node_cstr(patternValue);
    xt_glob_run(pattern, cwd, withFileTypes, &list);
  }

  xt_value result = xt_array_new((int32_t)list.count, list.items);
  free(list.items);
  return result;
}
