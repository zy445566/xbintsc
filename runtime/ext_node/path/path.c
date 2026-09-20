/*
 * Node.js `path` module for xbintsc.
 *
 * Exposed through namespace dispatch: the compiler lowers `path.<name>(...)`
 * to `xt_path_static(<name>, argc, argv)`. Paths are produced with the POSIX
 * separator (`/`) on every platform, which Windows accepts everywhere, so the
 * rest of the compiler can treat results uniformly.
 *
 * On Windows the inputs may still arrive in native form (`C:\dir\file.ts`),
 * so both `/` and `\` are recognised as separators there and a drive prefix
 * (`C:`) is preserved. On POSIX only `/` separates path segments.
 *
 * Implemented: join, resolve, normalize, dirname, basename, extname,
 *              isAbsolute, relative.
 */

#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#define xt_path_getcwd _getcwd
#else
#include <unistd.h>
#define xt_path_getcwd getcwd
#endif

#define XT_PATH_MAX 4096

/** True when `c` separates path segments on this platform. */
static int xt_path_is_sep(char c) {
#if defined(_WIN32)
  return c == '/' || c == '\\';
#else
  return c == '/';
#endif
}

/** Length of the leading root (`/`, `C:/` or `C:\`) or 0 when there is none. */
static size_t xt_path_root_len(const char *path) {
#if defined(_WIN32)
  if (path[0] == '\0') return 0;
  int drive = ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
              path[1] == ':';
  if (drive) return xt_path_is_sep(path[2]) ? 3 : 2;
#endif
  return xt_path_is_sep(path[0]) ? 1 : 0;
}

static int xt_path_is_absolute(const char *path) {
  if (!path || path[0] == '\0') return 0;
#if defined(_WIN32)
  size_t root = xt_path_root_len(path);
  /* A drive without a following separator (`C:`) is drive-relative, not absolute. */
  if (path[0] != '\0' && path[1] == ':') return root == 3;
  return xt_path_is_sep(path[0]) ? 1 : 0;
#else
  return path[0] == '/';
#endif
}

static void xt_path_normalize(const char *input, char *out, size_t size) {
  size_t prefixLen = 0;
  char prefix[4] = {0};
  const char *body = input;

#if defined(_WIN32)
  if (((input[0] >= 'A' && input[0] <= 'Z') || (input[0] >= 'a' && input[0] <= 'z')) &&
      input[1] == ':') {
    prefix[0] = input[0];
    prefix[1] = ':';
    prefixLen = 2;
    body = input + 2;
  }
#endif

  int absolute = xt_path_is_sep(body[0]);
  size_t length = strlen(body);
  char *copy = (char *)malloc(length + 1);
  if (!copy) {
    out[0] = '\0';
    return;
  }
  memcpy(copy, body, length + 1);

  size_t capacity = 8;
  size_t count = 0;
  char **segments = (char **)malloc(sizeof(char *) * capacity);
  if (!segments) {
    free(copy);
    out[0] = '\0';
    return;
  }

  size_t i = 0;
  while (i < length) {
    while (i < length && xt_path_is_sep(copy[i])) i++;
    if (i >= length) break;
    size_t start = i;
    while (i < length && !xt_path_is_sep(copy[i])) i++;
    if (i < length) {
      copy[i] = '\0';
      i++;
    } else {
      copy[i] = '\0';
    }
    char *segment = copy + start;
    if (strcmp(segment, ".") == 0) continue;
    if (strcmp(segment, "..") == 0) {
      if (count > 0 && strcmp(segments[count - 1], "..") != 0) {
        count--;
      } else if (!absolute) {
        if (count == capacity) {
          capacity *= 2;
          segments = (char **)realloc(segments, sizeof(char *) * capacity);
        }
        segments[count++] = segment;
      }
      continue;
    }
    if (count == capacity) {
      capacity *= 2;
      segments = (char **)realloc(segments, sizeof(char *) * capacity);
    }
    segments[count++] = segment;
  }

  size_t position = 0;
  for (size_t p = 0; p < prefixLen && position + 1 < size; p++) out[position++] = prefix[p];
  if (absolute && position + 1 < size) out[position++] = '/';
  for (size_t s = 0; s < count; s++) {
    size_t segmentLength = strlen(segments[s]);
    if (position + segmentLength + 2 > size) break;
    if (s > 0) out[position++] = '/';
    memcpy(out + position, segments[s], segmentLength);
    position += segmentLength;
  }
  out[position] = '\0';
  if (position == 0) {
    out[0] = '.';
    out[1] = '\0';
  }
  free(segments);
  free(copy);
}

static void xt_path_absolute(const char *path, char *out, size_t size) {
  if (xt_path_is_absolute(path)) {
    snprintf(out, size, "%s", path);
  } else {
    char cwd[XT_PATH_MAX];
    if (!xt_path_getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
    snprintf(out, size, "%s/%s", cwd, path);
  }
  char normalized[XT_PATH_MAX];
  xt_path_normalize(out, normalized, sizeof(normalized));
  snprintf(out, size, "%s", normalized);
}

typedef struct {
  char *storage;
  char **segments;
  size_t count;
} xt_path_parts;

static void xt_path_split(const char *path, xt_path_parts *parts) {
  size_t length = strlen(path);
  parts->storage = (char *)malloc(length + 1);
  parts->count = 0;
  parts->segments = NULL;
  if (!parts->storage) return;
  memcpy(parts->storage, path, length + 1);

  size_t capacity = 8;
  parts->segments = (char **)malloc(sizeof(char *) * capacity);
  if (!parts->segments) return;

  size_t i = 0;
  while (i < length) {
    while (i < length && xt_path_is_sep(parts->storage[i])) i++;
    if (i >= length) break;
    size_t start = i;
    while (i < length && !xt_path_is_sep(parts->storage[i])) i++;
    if (i < length) {
      parts->storage[i] = '\0';
      i++;
    } else {
      parts->storage[i] = '\0';
    }
    if (parts->count == capacity) {
      capacity *= 2;
      parts->segments = (char **)realloc(parts->segments, sizeof(char *) * capacity);
    }
    parts->segments[parts->count++] = parts->storage + start;
  }
}

static void xt_path_parts_free(xt_path_parts *parts) {
  free(parts->segments);
  free(parts->storage);
}

/* ------------------------------------------------------------------------- */
/* Individual operations                                                     */
/* ------------------------------------------------------------------------- */

static xt_value xt_path_join(int32_t argc, xt_value *argv) {
  size_t total = 1;
  for (int32_t i = 0; i < argc; i++) {
    const char *part = xt_string_data(xt_to_string(argv[i]));
    if (part) total += strlen(part) + 1;
  }
  char *joined = (char *)malloc(total);
  if (!joined) return xt_undefined();
  joined[0] = '\0';
  for (int32_t i = 0; i < argc; i++) {
    const char *part = xt_string_data(xt_to_string(argv[i]));
    if (!part || part[0] == '\0') continue;
    if (joined[0] != '\0') strcat(joined, "/");
    strcat(joined, part);
  }
  char normalized[XT_PATH_MAX];
  xt_path_normalize(joined, normalized, sizeof(normalized));
  free(joined);
  return xt_string_from_cstr(normalized);
}

static xt_value xt_path_resolve(int32_t argc, xt_value *argv) {
  char result[XT_PATH_MAX];
  result[0] = '\0';
  for (int32_t i = 0; i < argc; i++) {
    const char *part = xt_string_data(xt_to_string(argv[i]));
    if (!part || part[0] == '\0') continue;
    if (xt_path_is_absolute(part)) {
      snprintf(result, sizeof(result), "%s", part);
    } else if (result[0] == '\0') {
      snprintf(result, sizeof(result), "%s", part);
    } else {
      size_t used = strlen(result);
      snprintf(result + used, sizeof(result) - used, "/%s", part);
    }
  }
  if (result[0] == '\0') {
    if (!xt_path_getcwd(result, sizeof(result))) result[0] = '\0';
  } else if (!xt_path_is_absolute(result)) {
    char cwd[XT_PATH_MAX];
    if (!xt_path_getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
    char combined[XT_PATH_MAX];
    snprintf(combined, sizeof(combined), "%s/%s", cwd, result);
    snprintf(result, sizeof(result), "%s", combined);
  }
  char normalized[XT_PATH_MAX];
  xt_path_normalize(result, normalized, sizeof(normalized));
  return xt_string_from_cstr(normalized);
}

static xt_value xt_path_dirname(const char *path) {
  size_t length = strlen(path);
  size_t root = xt_path_root_len(path);
  size_t end = length;
  while (end > root && xt_path_is_sep(path[end - 1])) end--;
  if (end <= root) {
    if (root == 0) return xt_string_from_cstr(".");
    return xt_string_new(path, root);
  }
  size_t i = end;
  while (i > root && !xt_path_is_sep(path[i - 1])) i--;
  size_t dirEnd = i;
  while (dirEnd > root && xt_path_is_sep(path[dirEnd - 1])) dirEnd--;
  if (dirEnd == 0) return xt_string_from_cstr(".");
  return xt_string_new(path, dirEnd);
}

static xt_value xt_path_basename(const char *path, const char *extension) {
  size_t length = strlen(path);
  while (length > 0 && xt_path_is_sep(path[length - 1])) length--;
  if (length == 0) return xt_string_from_cstr("");
  size_t start = length;
  while (start > 0 && !xt_path_is_sep(path[start - 1])) start--;
  size_t end = length;
  if (extension && extension[0] != '\0') {
    size_t extensionLength = strlen(extension);
    if (end - start > extensionLength &&
        strncmp(path + end - extensionLength, extension, extensionLength) == 0) {
      end -= extensionLength;
    }
  }
  return xt_string_new(path + start, end - start);
}

static xt_value xt_path_extname(const char *path) {
  size_t length = strlen(path);
  size_t start = 0;
  for (size_t i = 0; i < length; i++) {
    if (xt_path_is_sep(path[i])) start = i + 1;
  }
  size_t dot = length;
  for (size_t i = start; i < length; i++) {
    if (path[i] == '.') dot = i;
  }
  if (dot == length || dot == start) return xt_string_from_cstr("");
  return xt_string_new(path + dot, length - dot);
}

static xt_value xt_path_relative(const char *from, const char *to) {
  char fromAbsolute[XT_PATH_MAX];
  char toAbsolute[XT_PATH_MAX];
  xt_path_absolute(from, fromAbsolute, sizeof(fromAbsolute));
  xt_path_absolute(to, toAbsolute, sizeof(toAbsolute));

  xt_path_parts fromParts;
  xt_path_parts toParts;
  xt_path_split(fromAbsolute, &fromParts);
  xt_path_split(toAbsolute, &toParts);

  size_t common = 0;
  while (common < fromParts.count && common < toParts.count &&
         strcmp(fromParts.segments[common], toParts.segments[common]) == 0) {
    common++;
  }

  char result[XT_PATH_MAX];
  result[0] = '\0';
  size_t position = 0;
  for (size_t i = common; i < fromParts.count && position + 3 < sizeof(result); i++) {
    if (position > 0) result[position++] = '/';
    result[position++] = '.';
    result[position++] = '.';
  }
  for (size_t i = common; i < toParts.count; i++) {
    size_t segmentLength = strlen(toParts.segments[i]);
    if (position + segmentLength + 2 > sizeof(result)) break;
    if (position > 0) result[position++] = '/';
    memcpy(result + position, toParts.segments[i], segmentLength);
    position += segmentLength;
  }
  result[position] = '\0';

  xt_path_parts_free(&fromParts);
  xt_path_parts_free(&toParts);
  return xt_string_from_cstr(result);
}

/* ------------------------------------------------------------------------- */
/* Dispatch                                                                  */
/* ------------------------------------------------------------------------- */

xt_value xt_path_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(name);
  if (!method) return xt_undefined();

  if (strcmp(method, "join") == 0) return xt_path_join(argc, argv);
  if (strcmp(method, "resolve") == 0) return xt_path_resolve(argc, argv);
  if (strcmp(method, "isAbsolute") == 0) {
    const char *path = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : NULL;
    return xt_bool(xt_path_is_absolute(path));
  }
  if (strcmp(method, "normalize") == 0) {
    const char *path = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : "";
    char normalized[XT_PATH_MAX];
    xt_path_normalize(path ? path : "", normalized, sizeof(normalized));
    return xt_string_from_cstr(normalized);
  }
  if (strcmp(method, "dirname") == 0) {
    const char *path = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : "";
    return xt_path_dirname(path ? path : "");
  }
  if (strcmp(method, "basename") == 0) {
    const char *path = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : "";
    const char *extension = argc > 1 ? xt_string_data(xt_to_string(argv[1])) : NULL;
    return xt_path_basename(path ? path : "", extension);
  }
  if (strcmp(method, "extname") == 0) {
    const char *path = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : "";
    return xt_path_extname(path ? path : "");
  }
  if (strcmp(method, "relative") == 0) {
    const char *from = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : "";
    const char *to = argc > 1 ? xt_string_data(xt_to_string(argv[1])) : "";
    return xt_path_relative(from ? from : "", to ? to : "");
  }
  return xt_undefined();
}
