/* -- request / response exchange ------------------------------------------ */

/*
 * Request serialisation, response framing and header parsing for `fetch`. The
 * blocking exchange itself runs on an async I/O worker (see `async.inc`); these
 * helpers are shared by the main thread (building the request, parsing the
 * completed response) and the worker (deciding when the response is complete).
 */

/** Index just past the blank line that terminates the response headers. */
static int xt_fetch_head_end(const xt_fetch_buffer *raw) {
  for (size_t i = 0; i + 3 < raw->length; i++) {
    if (raw->data[i] == '\r' && raw->data[i + 1] == '\n' && raw->data[i + 2] == '\r' && raw->data[i + 3] == '\n') {
      return (int)(i + 4);
    }
  }
  return -1;
}

static int xt_fetch_hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/** 1 when the chunked body is complete, 0 when more bytes are needed, -1 on a
 * malformed encoding. `decoded` accumulates the total decoded body length. */
static int xt_fetch_chunked_complete(const char *data, size_t length, size_t offset, size_t *decoded) {
  size_t pos = offset;
  *decoded = 0;
  for (;;) {
    if (pos >= length) return 0;
    size_t lineEnd = pos;
    while (lineEnd + 1 < length && !(data[lineEnd] == '\r' && data[lineEnd + 1] == '\n')) lineEnd++;
    if (lineEnd + 1 >= length) return 0;
    size_t size = 0;
    int any = 0;
    for (size_t k = pos; k < lineEnd; k++) {
      int digit = xt_fetch_hex_digit(data[k]);
      if (digit < 0) break;
      size = size * 16 + (size_t)digit;
      any = 1;
      if (size > ((size_t)1 << 30)) return -1;
    }
    if (!any) return -1;
    size_t dataStart = lineEnd + 2;
    if (size == 0) {
      if (dataStart + 1 < length && data[dataStart] == '\r' && data[dataStart + 1] == '\n') return 1;
      for (size_t i = dataStart; i + 3 < length; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') return 1;
      }
      return 0;
    }
    if (dataStart + size + 1 >= length) return 0;
    if (data[dataStart + size] != '\r' || data[dataStart + size + 1] != '\n') return -1;
    *decoded += size;
    pos = dataStart + size + 2;
  }
}

/** Copy the decoded payload of a complete chunked body into `out`. */
static void xt_fetch_decode_chunked(const char *data, size_t length, size_t offset, xt_fetch_buffer *out) {
  size_t pos = offset;
  for (;;) {
    size_t lineEnd = pos;
    while (lineEnd + 1 < length && !(data[lineEnd] == '\r' && data[lineEnd + 1] == '\n')) lineEnd++;
    size_t size = 0;
    for (size_t k = pos; k < lineEnd; k++) {
      int digit = xt_fetch_hex_digit(data[k]);
      if (digit < 0) break;
      size = size * 16 + (size_t)digit;
    }
    size_t dataStart = lineEnd + 2;
    if (size == 0) return;
    if (dataStart + size <= length) xt_fetch_buffer_append(out, data + dataStart, size);
    pos = dataStart + size + 2;
    if (pos >= length) return;
  }
}

static int xt_fetch_name_equals(const char *name, size_t length, const char *literal) {
  size_t end = length;
  while (end > 0 && (name[end - 1] == ' ' || name[end - 1] == '\t')) end--;
  if (end != strlen(literal)) return 0;
  for (size_t i = 0; i < end; i++) {
    char a = name[i];
    char b = literal[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
    if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
    if (a != b) return 0;
  }
  return 1;
}

/** Pull `Content-Length` / `Transfer-Encoding` out of the raw header block. */
static void xt_fetch_scan_headers(const char *data, size_t start, size_t end, size_t *contentLength,
                                  int *hasContentLength, int *chunked) {
  *contentLength = 0;
  *hasContentLength = 0;
  *chunked = 0;
  size_t i = start;
  while (i < end) {
    size_t lineEnd = i;
    while (lineEnd + 1 < end && !(data[lineEnd] == '\r' && data[lineEnd + 1] == '\n')) lineEnd++;
    if (lineEnd + 1 > end) break;
    const char *colon = (const char *)memchr(data + i, ':', lineEnd - i);
    if (colon) {
      size_t nameLength = (size_t)(colon - (data + i));
      const char *value = colon + 1;
      const char *valueEnd = data + lineEnd;
      while (value < valueEnd && (*value == ' ' || *value == '\t')) value++;
      if (xt_fetch_name_equals(data + i, nameLength, "content-length")) {
        size_t parsed = 0;
        for (const char *p = value; p < valueEnd && *p >= '0' && *p <= '9'; p++) parsed = parsed * 10 + (size_t)(*p - '0');
        *contentLength = parsed;
        *hasContentLength = 1;
      } else if (xt_fetch_name_equals(data + i, nameLength, "transfer-encoding")) {
        char lower[128];
        size_t n = (size_t)(valueEnd - value);
        if (n >= sizeof(lower)) n = sizeof(lower) - 1;
        for (size_t k = 0; k < n; k++) {
          char c = value[k];
          lower[k] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        }
        lower[n] = '\0';
        if (strstr(lower, "chunked")) *chunked = 1;
      }
    }
    if (lineEnd + 2 <= i) break;
    i = lineEnd + 2;
  }
}

/** Parse `HTTP/1.1 200 OK`; `headerStart` receives the offset of the headers. */
static int xt_fetch_parse_status_line(const char *data, size_t length, int *status, char *text, size_t textSize,
                                      size_t *headerStart) {
  size_t lineEnd = 0;
  while (lineEnd + 1 < length && !(data[lineEnd] == '\r' && data[lineEnd + 1] == '\n')) lineEnd++;
  if (lineEnd + 1 >= length) return -1;
  *headerStart = lineEnd + 2;
  size_t i = 0;
  while (i < lineEnd && data[i] != ' ') i++;
  if (i >= lineEnd) return -1;
  i++;
  int code = 0;
  size_t digits = 0;
  while (i < lineEnd && data[i] >= '0' && data[i] <= '9') {
    code = code * 10 + (data[i] - '0');
    i++;
    digits++;
  }
  if (digits < 3) return -1;
  while (i < lineEnd && data[i] == ' ') i++;
  size_t j = 0;
  while (i < lineEnd && j + 1 < textSize) text[j++] = data[i++];
  text[j] = '\0';
  *status = code;
  return 0;
}

static int xt_fetch_method_has_body(const char *method) {
  return strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0;
}

static void xt_fetch_build_request(const char *method, const xt_fetch_url *url, xt_value headers,
                                   const xt_fetch_buffer *body, xt_fetch_buffer *out) {
  xt_fetch_buffer_append_cstr(out, method);
  xt_fetch_buffer_append_cstr(out, " ");
  xt_fetch_buffer_append_cstr(out, url->path);
  xt_fetch_buffer_append_cstr(out, " HTTP/1.1\r\n");

  char hostHeader[300];
  int defaultPort = url->secure ? 443 : 80;
  if (atoi(url->port) == defaultPort) {
    snprintf(hostHeader, sizeof(hostHeader), "%s", url->host);
  } else {
    snprintf(hostHeader, sizeof(hostHeader), "%s:%s", url->host, url->port);
  }
  if (!xt_headers_lookup(headers, "host")) xt_headers_set_raw(headers, "host", hostHeader);
  if (!xt_headers_lookup(headers, "connection")) xt_headers_set_raw(headers, "connection", "close");
  if (!xt_headers_lookup(headers, "accept")) xt_headers_set_raw(headers, "accept", "*/*");
  if (!xt_headers_lookup(headers, "accept-encoding")) xt_headers_set_raw(headers, "accept-encoding", "identity");
  if (!xt_headers_lookup(headers, "user-agent")) xt_headers_set_raw(headers, "user-agent", "xbintsc-fetch");
  if (!xt_headers_lookup(headers, "content-length") && (body || xt_fetch_method_has_body(method))) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%zu", body ? body->length : (size_t)0);
    xt_headers_set_raw(headers, "content-length", buffer);
  }

  xt_value map = xt_headers_map(headers);
  xt_value keys = xt_object_keys(map);
  int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
  for (int32_t i = 0; i < count; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    const char *name = xt_string_data(key);
    const char *value = xt_string_data(xt_to_string(xt_object_get(map, key)));
    if (!name || !value) continue;
    xt_fetch_buffer_append_cstr(out, name);
    xt_fetch_buffer_append_cstr(out, ": ");
    xt_fetch_buffer_append_cstr(out, value);
    xt_fetch_buffer_append_cstr(out, "\r\n");
  }
  xt_fetch_buffer_append_cstr(out, "\r\n");
  if (body && body->length > 0) xt_fetch_buffer_append(out, body->data, body->length);
}

/* The async exchange itself lives in `async.inc`; it reuses the parsers and
 * request builder above. */
