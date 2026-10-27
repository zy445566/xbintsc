/*
 * xbintsc runtime — URL.
 *
 * Relative-reference resolution against a base URL, plus the read-only
 * components a compiled page or CLI tool typically reads. The WHATWG URL
 * constructor is `URL`; `URLSearchParams` lives in xt_url.c.
 *
 * A reference with no scheme is a filesystem path: it resolves against a
 * `file://` base (the working directory when the caller supplied no base), so
 * `new URL("./model.glb", import.meta.url)` and `new URL("./model.glb")`
 * both produce a usable file URL. `fetch` uses the same rule for relative
 * requests (see `xt_url_file_path`), which is how a compiled page can load a
 * local asset without going through an HTTP server.
 */

#include "rt_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define xt_url_getcwd _getcwd
#else
#include <unistd.h>
#define xt_url_getcwd getcwd
#endif

/* URLSearchParams is defined in xt_url.c. */
extern xt_value xt_url_search_params_ctor(int32_t argc, xt_value *argv);

/* -- URL ------------------------------------------------------------------ */

/*
 * A deliberately small WHATWG-ish `URL`: resolution of relative references
 * against a base (the shape a browser page and `import.meta.url` produce),
 * plus the read-only components the games and tools we compile actually use.
 * It is not a full URL parser: percent-encoding of the path, IDNA hosts and
 * the `URL` setters are out of scope.
 */

/** Longest URL this runtime will build. Longer inputs are truncated. */
#define XT_URL_MAX 8192

/** Length of a leading `scheme:` prefix, or 0 when `text` has no scheme. */
static size_t xt_url_scheme_length(const char *text) {
  if (!isalpha((unsigned char)text[0])) return 0;
  size_t i = 1;
  for (; text[i]; i++) {
    if (text[i] == ':') {
#if defined(_WIN32)
      /* A drive letter (`C:\dir` / `C:/dir`) is a path, not a scheme. */
      if (i == 1 && (text[2] == '\\' || text[2] == '/')) return 0;
#endif
      return i;
    }
    unsigned char c = (unsigned char)text[i];
    if (!(isalnum(c) || c == '+' || c == '-' || c == '.')) return 0;
  }
  return 0;
}

/** Join `scheme://authority` (already written) with an absolute path. */
static void xt_url_append(char *out, size_t size, const char *text) {
  size_t used = strlen(out);
  size_t length = strlen(text);
  if (used + length >= size) length = used < size ? size - used - 1 : 0;
  memcpy(out + used, text, length);
  out[used + length] = 0;
}

/** Normalize `.` and `..` segments in `path` into `out`. */
static void xt_url_normalize_path(const char *path, char *out, size_t size) {
  int absolute = path[0] == '/';
  size_t length = strlen(path);
  int trailing = length > 0 && path[length - 1] == '/';
  size_t write = 0;
  if (absolute && write + 1 < size) out[write++] = '/';
  const char *cursor = path;
  while (*cursor == '/') cursor++;
  while (*cursor) {
    const char *start = cursor;
    while (*cursor && *cursor != '/') cursor++;
    size_t segment = (size_t)(cursor - start);
    if (segment == 1 && start[0] == '.') {
      /* `./` adds nothing. */
    } else if (segment == 2 && start[0] == '.' && start[1] == '.') {
      size_t floor = absolute ? 1 : 0;
      if (write > floor) {
        while (write > floor && out[write - 1] != '/') write--;
        if (write > floor && out[write - 1] == '/') write--;
      }
    } else if (segment > 0) {
      if (write > 0 && out[write - 1] != '/' && write + 1 < size) out[write++] = '/';
      size_t copy = segment;
      if (write + copy >= size) copy = write < size ? size - write - 1 : 0;
      memcpy(out + write, start, copy);
      write += copy;
    }
    while (*cursor == '/') cursor++;
  }
  if (trailing && write > 0 && out[write - 1] != '/' && write + 1 < size) out[write++] = '/';
  if (write == 0 && size > 1) out[write++] = '/';
  out[write < size ? write : size - 1] = 0;
}

/** The `file://` URL of the working directory, with a trailing slash. */
static void xt_url_cwd_base(char *out, size_t size) {
  char cwd[4096];
  cwd[0] = 0;
  if (xt_url_getcwd(cwd, sizeof(cwd)) == NULL) cwd[0] = 0;
  size_t length = strlen(cwd);
  while (length > 1 && (cwd[length - 1] == '/' || cwd[length - 1] == '\\')) cwd[--length] = 0;
  /* Windows reports `C:\dir`; a file URL spells that `/C:/dir`. */
  char path[4300];
  size_t j = 0;
#if defined(_WIN32)
  if (length > 0 && cwd[0] != '/') path[j++] = '/';
#endif
  for (size_t i = 0; i < length && j + 1 < sizeof(path); i++) {
    path[j++] = cwd[i] == '\\' ? '/' : cwd[i];
  }
  path[j] = 0;
  if (j == 0 || path[j - 1] != '/') {
    if (j + 1 < sizeof(path)) {
      path[j++] = '/';
      path[j] = 0;
    }
  }
  snprintf(out, size, "file://%s", path);
}

/** Build `href` for `input` resolved against `base` (either may be empty). */
static void xt_url_resolve(const char *input, const char *base, char *out, size_t size) {
  out[0] = 0;
  if (input[0] == 0) {
    snprintf(out, size, "%s", base);
    return;
  }
  if (xt_url_scheme_length(input) > 0) {
    snprintf(out, size, "%s", input);
    return;
  }
  if (base[0] == 0) {
    snprintf(out, size, "%s", input);
    return;
  }

  size_t schemeLength = xt_url_scheme_length(base);
  if (schemeLength == 0) {
    snprintf(out, size, "%s", input);
    return;
  }
  int hasAuthority = base[schemeLength + 1] == '/' && base[schemeLength + 2] == '/';
  if (input[0] == '/' && input[1] == '/') {
    snprintf(out, size, "%.*s:%s", (int)schemeLength, base, input);
    return;
  }
  const char *authorityStart = base + schemeLength + (hasAuthority ? 3 : 1);
  const char *authorityEnd = authorityStart;
  while (*authorityEnd && *authorityEnd != '/' && *authorityEnd != '?' && *authorityEnd != '#') authorityEnd++;
  const char *pathStart = authorityEnd;
  if (*pathStart != '/') pathStart = "";

  /* scheme: + //authority — the fixed prefix every resolved URL keeps. */
  size_t prefix = 0;
  if (prefix + schemeLength + 1 < size) {
    memcpy(out + prefix, base, schemeLength);
    prefix += schemeLength;
    out[prefix++] = ':';
  }
  if (hasAuthority) {
    if (prefix + 2 < size) {
      out[prefix++] = '/';
      out[prefix++] = '/';
    }
    size_t authorityLength = (size_t)(authorityEnd - authorityStart);
    if (prefix + authorityLength >= size) authorityLength = size - prefix - 1;
    memcpy(out + prefix, authorityStart, authorityLength);
    prefix += authorityLength;
  }
  out[prefix] = 0;

  /* Split the reference into its path and its `?query#fragment` suffix. */
  const char *suffix = input;
  while (*suffix && *suffix != '?' && *suffix != '#') suffix++;
  size_t inputPathLength = (size_t)(suffix - input);

  char joined[XT_URL_MAX];
  joined[0] = 0;
  if (input[0] == '/') {
    if (inputPathLength >= sizeof(joined)) inputPathLength = sizeof(joined) - 1;
    memcpy(joined, input, inputPathLength);
    joined[inputPathLength] = 0;
  } else {
    size_t basePathLength = 0;
    const char *basePathEnd = pathStart;
    while (*basePathEnd && *basePathEnd != '?' && *basePathEnd != '#') basePathEnd++;
    basePathLength = (size_t)(basePathEnd - pathStart);
    size_t directory = 0;
    for (size_t i = 0; i < basePathLength; i++) {
      if (pathStart[i] == '/') directory = i + 1;
    }
    if (directory == 0 && hasAuthority) directory = 0;
    if (directory >= sizeof(joined)) directory = sizeof(joined) - 1;
    memcpy(joined, pathStart, directory);
    joined[directory] = 0;
    size_t room = sizeof(joined) - directory - 1;
    if (inputPathLength > room) inputPathLength = room;
    memcpy(joined + directory, input, inputPathLength);
    joined[directory + inputPathLength] = 0;
  }

  char normalized[XT_URL_MAX];
  xt_url_normalize_path(joined, normalized, sizeof(normalized));
  xt_url_append(out, size, normalized);
  xt_url_append(out, size, suffix);
}

/** Publish the read-only components parsed from `href` on the URL object. */
static void xt_url_set_components(xt_value url, const char *href) {
  size_t schemeLength = xt_url_scheme_length(href);
  char scheme[32];
  if (schemeLength >= sizeof(scheme)) schemeLength = sizeof(scheme) - 1;
  memcpy(scheme, href, schemeLength);
  scheme[schemeLength] = 0;
  char protocol[36];
  snprintf(protocol, sizeof(protocol), "%s:", scheme);

  const char *rest = href + schemeLength + (schemeLength > 0 && href[schemeLength] == ':' ? 1 : 0);
  int hasAuthority = rest[0] == '/' && rest[1] == '/';
  const char *authorityStart = rest + (hasAuthority ? 2 : 0);
  const char *authorityEnd = authorityStart;
  while (*authorityEnd && *authorityEnd != '/' && *authorityEnd != '?' && *authorityEnd != '#') authorityEnd++;
  const char *pathStart = authorityEnd;
  const char *pathEnd = pathStart;
  while (*pathEnd && *pathEnd != '?' && *pathEnd != '#') pathEnd++;
  const char *search = (*pathEnd == '?') ? pathEnd : "";
  const char *searchEnd = search;
  if (*search) while (*searchEnd && *searchEnd != '#') searchEnd++;
  const char *hash = (*searchEnd == '#') ? searchEnd : (pathEnd && *pathEnd == '#' ? pathEnd : "");

  char authority[512];
  size_t authorityLength = (size_t)(authorityEnd - authorityStart);
  if (authorityLength >= sizeof(authority)) authorityLength = sizeof(authority) - 1;
  memcpy(authority, authorityStart, authorityLength);
  authority[authorityLength] = 0;

  char hostname[512];
  snprintf(hostname, sizeof(hostname), "%s", authority);
  char *colon = strrchr(hostname, ':');
  char port[16] = "";
  if (colon && (colon > hostname) && (colon[1] == 0 || (colon[1] >= '0' && colon[1] <= '9'))) {
    snprintf(port, sizeof(port), "%s", colon + 1);
    *colon = 0;
  }
  char pathname[XT_URL_MAX];
  size_t pathLength = (size_t)(pathEnd - pathStart);
  if (pathLength >= sizeof(pathname)) pathLength = sizeof(pathname) - 1;
  memcpy(pathname, pathStart, pathLength);
  pathname[pathLength] = 0;
  char searchText[XT_URL_MAX];
  size_t searchLength = (size_t)(searchEnd - search);
  if (searchLength >= sizeof(searchText)) searchLength = sizeof(searchText) - 1;
  memcpy(searchText, search, searchLength);
  searchText[searchLength] = 0;
  char hashText[XT_URL_MAX];
  snprintf(hashText, sizeof(hashText), "%s", hash);
  char origin[XT_URL_MAX];
  if (!hasAuthority) snprintf(origin, sizeof(origin), "%s", protocol);
  else if (strcmp(scheme, "file") == 0) snprintf(origin, sizeof(origin), "null");
  else snprintf(origin, sizeof(origin), "%s://%s", scheme, authority);

  xt_object_set(url, xt_string_from_cstr("href"), xt_string_from_cstr(href));
  xt_object_set(url, xt_string_from_cstr("protocol"), xt_string_from_cstr(protocol));
  xt_object_set(url, xt_string_from_cstr("origin"), xt_string_from_cstr(origin));
  xt_object_set(url, xt_string_from_cstr("host"), xt_string_from_cstr(authority));
  xt_object_set(url, xt_string_from_cstr("hostname"), xt_string_from_cstr(hostname));
  xt_object_set(url, xt_string_from_cstr("port"), xt_string_from_cstr(port));
  xt_object_set(url, xt_string_from_cstr("pathname"), xt_string_from_cstr(pathname));
  xt_object_set(url, xt_string_from_cstr("search"), xt_string_from_cstr(searchText));
  xt_object_set(url, xt_string_from_cstr("hash"), xt_string_from_cstr(hashText));

  /* `searchParams` is a snapshot of `search`; the WHATWG live link between the
   * two is out of scope, like the `URL` setters. */
  xt_value query = xt_string_from_cstr(searchText);
  xt_object_set(url, xt_string_from_cstr("searchParams"), xt_url_search_params_ctor(1, &query));
}

static xt_value xt_url_method_href(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get(thisValue, xt_string_from_cstr("href"));
}

xt_value xt_url_ctor(int32_t argc, xt_value *argv) {
  /* `new URL(input, base)`: a base may itself be a URL object. */
  xt_value inputValue = xt_arg_at(argc, argv, 0);
  xt_value baseValue = xt_arg_at(argc, argv, 1);
  xt_value input = inputValue == XT_UNDEFINED ? xt_string_from_cstr("") : xt_to_string(inputValue);
  char base[XT_URL_MAX];
  base[0] = 0;
  if (baseValue != XT_UNDEFINED) {
    xt_value href = XT_IS_OBJECT(baseValue)
                        ? xt_object_get(baseValue, xt_string_from_cstr("href"))
                        : xt_to_string(baseValue);
    snprintf(base, sizeof(base), "%s", href == XT_UNDEFINED ? "" : xt_string_data(href));
  }
  const char *inputText = xt_string_data(input);
  if (!inputText) inputText = "";

  /* A reference without a scheme is a filesystem path. With no explicit base
   * it resolves against the working directory, so `new URL("./a.txt")` is a
   * `file://` URL rather than an unparseable relative one. */
  char adjusted[XT_URL_MAX];
  if (base[0] == 0 && xt_url_scheme_length(inputText) == 0) {
    xt_url_cwd_base(base, sizeof(base));
  }
#if defined(_WIN32)
  if (xt_url_scheme_length(inputText) == 0 && isalpha((unsigned char)inputText[0]) &&
      inputText[1] == ':' && (inputText[2] == '\\' || inputText[2] == '/')) {
    /* `C:\dir\file` -> `/C:/dir/file`, the path part of a file URL. */
    size_t j = 0;
    adjusted[j++] = '/';
    for (size_t i = 0; inputText[i] && j + 1 < sizeof(adjusted); i++) {
      adjusted[j++] = inputText[i] == '\\' ? '/' : inputText[i];
    }
    adjusted[j] = 0;
    inputText = adjusted;
  }
#endif

  char href[XT_URL_MAX];
  xt_url_resolve(inputText, base, href, sizeof(href));
  xt_value url = xt_object_new();
  xt_url_set_components(url, href);
  xt_object_set(url, xt_string_from_cstr("toString"),
                xt_closure_new((void *)xt_url_method_href, 0, NULL));
  xt_object_set(url, xt_string_from_cstr("toJSON"),
                xt_closure_new((void *)xt_url_method_href, 0, NULL));
  return url;
}

/* -- filesystem paths ----------------------------------------------------- */

static int xt_url_hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/** Append the percent-decoded `text` to `out` (a decoded `%2F` stays escaped). */
static void xt_url_append_decoded(char *out, size_t size, const char *text, size_t length) {
  size_t used = strlen(out);
  for (size_t i = 0; i < length && used + 2 < size; i++) {
    if (text[i] == '%' && i + 2 < length && xt_url_hex(text[i + 1]) >= 0 && xt_url_hex(text[i + 2]) >= 0) {
      out[used++] = (char)((xt_url_hex(text[i + 1]) << 4) | xt_url_hex(text[i + 2]));
      i += 2;
    } else {
      out[used++] = text[i] == '\\' ? '/' : text[i];
    }
  }
  out[used] = 0;
}

/**
 * Turn a `fetch` reference into a filesystem path.
 *
 * Returns 1 and writes an absolute path for `file:` URLs and for relative
 * references with no scheme (resolved against the working directory), 0 for
 * another scheme. The `?query` / `#fragment` suffix is not part of the path.
 */
int xt_url_file_path(const char *url, char *out, size_t size) {
  if (!url) return 0;
  out[0] = 0;
  char work[XT_URL_MAX];
  snprintf(work, sizeof(work), "%s", url);
  char *fragment = work;
  while (*fragment && *fragment != '?' && *fragment != '#') fragment++;
  *fragment = 0;

  size_t schemeLength = xt_url_scheme_length(work);
  const char *path = work;
  if (schemeLength > 0) {
    if (schemeLength != 4 || strncmp(work, "file:", 5) != 0) return 0;
    path = work + 5;
    if (path[0] == '/' && path[1] == '/') {
      path += 2;
      const char *slash = strchr(path, '/');
      if (slash != NULL) {
        size_t hostLength = (size_t)(slash - path);
        int localhost = hostLength == 9 && strncmp(path, "localhost", 9) == 0;
        if (hostLength > 0 && !localhost) return 0;
        path = slash;
      } else {
        path = "/";
      }
    }
    xt_url_append_decoded(out, size, path, strlen(path));
  } else {
    /* Relative references are relative to the process working directory. */
    char cwd[4096];
    cwd[0] = 0;
    if (xt_url_getcwd(cwd, sizeof(cwd)) == NULL) cwd[0] = 0;
    int absolute = work[0] == '/' || work[0] == '\\';
#if defined(_WIN32)
    if (!absolute && isalpha((unsigned char)work[0]) && work[1] == ':' &&
        (work[2] == '/' || work[2] == '\\')) {
      absolute = 1;
    }
#endif
    if (!absolute && cwd[0] != 0) {
      snprintf(out, size, "%s/", cwd);
    }
    xt_url_append_decoded(out, size, work, strlen(work));
  }
#if defined(_WIN32)
  if (out[0] == '/' && isalpha((unsigned char)out[1]) && out[2] == ':') {
    memmove(out, out + 1, strlen(out));
  }
#endif
  return out[0] != 0;
}
