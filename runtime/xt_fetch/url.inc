/* -- URL parsing ---------------------------------------------------------- */

/*
 * `fetch` accepts absolute `http://` / `https://` URLs (there is no document
 * base to resolve relative ones against). The parsed pieces are kept in a
 * fixed-size struct because `fetch` hands them straight to `getaddrinfo`.
 */
typedef struct {
  int secure;
  char host[256];
  char port[8];
  char path[4096];
} xt_fetch_url;

/** Parse `url` into `out`. Returns 0 on success, -1 on a malformed URL. */
static int xt_fetch_parse_url(const char *url, xt_fetch_url *out) {
  if (!url) return -1;
  out->secure = 0;
  const char *cursor = url;
  if (strncmp(cursor, "http://", 7) == 0) {
    cursor += 7;
    snprintf(out->port, sizeof(out->port), "80");
  } else if (strncmp(cursor, "https://", 8) == 0) {
    cursor += 8;
    out->secure = 1;
    snprintf(out->port, sizeof(out->port), "443");
  } else {
    return -1;
  }

  /* The authority ends at the first `/`, `?` or `#` (or the end of the URL). */
  const char *authorityEnd = cursor;
  while (*authorityEnd && *authorityEnd != '/' && *authorityEnd != '?' && *authorityEnd != '#') authorityEnd++;

  /* Drop any `user:password@` prefix. */
  const char *hostStart = cursor;
  for (const char *p = cursor; p < authorityEnd; p++) {
    if (*p == '@') hostStart = p + 1;
  }

  const char *hostEnd = authorityEnd;
  const char *portStart = NULL;
  if (hostStart < authorityEnd && *hostStart == '[') {
    /* IPv6 literal: `[::1]:8080`. */
    const char *close = (const char *)memchr(hostStart, ']', (size_t)(authorityEnd - hostStart));
    if (!close) return -1;
    hostEnd = close;
    if (close + 1 < authorityEnd && close[1] == ':') portStart = close + 2;
    hostStart++;
  } else {
    const char *colon = (const char *)memchr(hostStart, ':', (size_t)(authorityEnd - hostStart));
    if (colon) {
      hostEnd = colon;
      portStart = colon + 1;
    }
  }

  size_t hostLength = (size_t)(hostEnd - hostStart);
  if (hostLength == 0 || hostLength >= sizeof(out->host)) return -1;
  memcpy(out->host, hostStart, hostLength);
  out->host[hostLength] = '\0';

  if (portStart && portStart < authorityEnd) {
    size_t portLength = (size_t)(authorityEnd - portStart);
    if (portLength >= sizeof(out->port)) return -1;
    memcpy(out->port, portStart, portLength);
    out->port[portLength] = '\0';
    for (const char *p = out->port; *p; p++) {
      if (*p < '0' || *p > '9') return -1;
    }
  }

  /* The request target keeps the query string but drops the fragment. */
  if (*authorityEnd == '\0' || *authorityEnd == '#') {
    snprintf(out->path, sizeof(out->path), "/");
  } else {
    const char *pathEnd = authorityEnd;
    while (*pathEnd && *pathEnd != '#') pathEnd++;
    size_t pathLength = (size_t)(pathEnd - authorityEnd);
    if (pathLength >= sizeof(out->path)) return -1;
    memcpy(out->path, authorityEnd, pathLength);
    out->path[pathLength] = '\0';
  }
  return 0;
}

/*
 * Resolve a `Location` header against the URL just requested. Only absolute
 * URLs and root-relative paths are supported; anything else is reported as a
 * failure so the caller can return the 3xx response unchanged.
 */
static int xt_fetch_resolve_location(const xt_fetch_url *base, const char *location, char *out, size_t size) {
  if (strncmp(location, "http://", 7) == 0 || strncmp(location, "https://", 8) == 0) {
    snprintf(out, size, "%s", location);
    return 0;
  }
  const char *scheme = base->secure ? "https" : "http";
  if (location[0] == '/') {
    int defaultPort = base->secure ? 443 : 80;
    if (atoi(base->port) == defaultPort) {
      snprintf(out, size, "%s://%s%s", scheme, base->host, location);
    } else {
      snprintf(out, size, "%s://%s:%s%s", scheme, base->host, base->port, location);
    }
    return 0;
  }
  return -1;
}
