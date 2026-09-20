/*
 * Node.js `url` module for xbintsc.
 *
 * Implements the two helpers the compiler itself relies on:
 *   - `pathToFileURL(path)`  -> URL object with an `href` property
 *   - `fileURLToPath(url)`   -> filesystem path
 *
 * Both accept the `file://` scheme and percent-encoded URLs. On Windows a
 * leading slash before a drive letter (`/C:/`) is removed, matching Node.
 */

#include "rt.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int url_hex(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static xt_value url_percent_decode(const char *in) {
  size_t length = strlen(in);
  char *out = (char *)malloc(length + 1);
  if (!out) return xt_string_from_cstr(in);
  size_t j = 0;
  for (size_t i = 0; i < length; i++) {
    if (in[i] == '%' && i + 2 < length && url_hex(in[i + 1]) >= 0 && url_hex(in[i + 2]) >= 0) {
      out[j++] = (char)((url_hex(in[i + 1]) << 4) | url_hex(in[i + 2]));
      i += 2;
    } else {
      out[j++] = in[i];
    }
  }
  out[j] = 0;
  xt_value result = xt_string_from_cstr(out);
  free(out);
  return result;
}

xt_value xt_url_path_to_file_url(int32_t argc, xt_value *argv) {
  const char *raw = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : "";
  if (!raw) raw = "";
  /* Resolve to an absolute path so the URL is comparable to `import.meta.url`
     (which is built from realpath) even when given a relative path. */
  char resolved[4096];
  const char *path = raw;
#if defined(_WIN32)
  if (_fullpath(resolved, raw, sizeof(resolved))) path = resolved;
#else
  if (realpath(raw, resolved)) path = resolved;
#endif
  char href[4300];
  size_t j = 0;
  const char *prefix = "file://";
  for (; *prefix && j < sizeof(href) - 1; prefix++) href[j++] = *prefix;
#if defined(_WIN32)
  if (j < sizeof(href) - 1) href[j++] = '/';
#endif
  for (const char *p = path; *p && j < sizeof(href) - 1; p++) href[j++] = (*p == '\\') ? '/' : *p;
  href[j] = 0;

  xt_value url = xt_object_new();
  xt_set(url, xt_string_from_cstr("href"), xt_string_from_cstr(href));
  xt_set(url, xt_string_from_cstr("protocol"), xt_string_from_cstr("file:"));
  xt_set(url, xt_string_from_cstr("pathname"), xt_string_from_cstr(path));
  return url;
}

xt_value xt_url_file_url_to_path(int32_t argc, xt_value *argv) {
  const char *url = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : "";
  const char *path = url;
  if (strncmp(url, "file://", 7) == 0) {
    path = url + 7;
    /* `file://localhost/...` is equivalent to `file:///...`. */
    if (strncmp(path, "localhost", 9) == 0) path += 9;
  }
#if defined(_WIN32)
  if (path[0] == '/' && isalpha((unsigned char)path[1]) && path[2] == ':') path++;
#endif
  return url_percent_decode(path);
}
