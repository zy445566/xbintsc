/* -- request / response exchange ------------------------------------------ */

/*
 * The actual `fetch` implementation. Because the runtime has no event loop,
 * this is a synchronous, blocking HTTP/1.1 exchange; the promise it returns is
 * already settled, which is all the current `await` machinery needs.
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

static xt_value xt_fetch_reject(const char *message) {
  xt_value argument = xt_string_from_cstr(message);
  return xt_promise_reject(xt_type_error_ctor(1, &argument));
}

xt_value xt_fetch(int32_t argc, xt_value *argv) {
  xt_value input = xt_arg_at(argc, argv, 0);
  xt_value init = xt_arg_at(argc, argv, 1);
  if (input == XT_UNDEFINED || input == XT_NULL) return xt_fetch_reject("Failed to parse URL from undefined");

  xt_value urlValue = input;
  if (XT_IS_OBJECT(input)) {
    xt_value candidate = xt_object_get_cstr(input, "url");
    if (XT_IS_STRING(candidate)) urlValue = candidate;
  }
  const char *urlText = xt_string_data(xt_to_string(urlValue));
  if (!urlText) return xt_fetch_reject("Failed to parse URL");

  char currentUrl[4096];
  snprintf(currentUrl, sizeof(currentUrl), "%s", urlText);

  char method[16];
  snprintf(method, sizeof(method), "GET");
  xt_value headersSource = XT_UNDEFINED;
  xt_value bodySource = XT_UNDEFINED;
  const char *redirectMode = "follow";
  if (XT_IS_OBJECT(init)) {
    xt_value methodValue = xt_object_get_cstr(init, "method");
    if (XT_IS_STRING(methodValue)) {
      const char *text = xt_string_data(methodValue);
      size_t i = 0;
      for (; text && text[i] && i + 1 < sizeof(method); i++) {
        char c = text[i];
        method[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
      }
      method[i] = '\0';
    }
    headersSource = xt_object_get_cstr(init, "headers");
    bodySource = xt_object_get_cstr(init, "body");
    xt_value redirectValue = xt_object_get_cstr(init, "redirect");
    if (XT_IS_STRING(redirectValue)) redirectMode = xt_string_data(redirectValue);
  }

  xt_fetch_buffer body;
  xt_fetch_buffer_init(&body);
  if (bodySource != XT_UNDEFINED && bodySource != XT_NULL) {
    xt_value text = xt_to_string(bodySource);
    const char *data = xt_string_data(text);
    if (data) xt_fetch_buffer_append(&body, data, (size_t)xt_string_length_value(text));
  }

  for (int redirects = 0; redirects <= 20; redirects++) {
    xt_fetch_url parsed;
    if (xt_fetch_parse_url(currentUrl, &parsed) != 0) {
      xt_fetch_buffer_free(&body);
      return xt_fetch_reject("Failed to parse URL");
    }
    if (parsed.secure) {
      xt_fetch_buffer_free(&body);
      return xt_fetch_reject("fetch failed: https is not supported");
    }

    int fd = xt_fetch_connect(&parsed);
    if (fd < 0) {
      xt_fetch_buffer_free(&body);
      return xt_fetch_reject("fetch failed");
    }

    xt_value requestHeaders = xt_headers_from_source(headersSource);
    xt_fetch_buffer request;
    xt_fetch_buffer_init(&request);
    xt_fetch_build_request(method, &parsed, requestHeaders, body.length > 0 ? &body : NULL, &request);
    if (xt_fetch_send_all(fd, request.data, request.length) != 0) {
      xt_fetch_close_socket(fd);
      xt_fetch_buffer_free(&request);
      xt_fetch_buffer_free(&body);
      return xt_fetch_reject("fetch failed");
    }
    xt_fetch_buffer_free(&request);

    xt_fetch_buffer raw;
    xt_fetch_buffer_init(&raw);
    char chunk[16384];
    for (;;) {
      int headEnd = xt_fetch_head_end(&raw);
      if (headEnd > 0) {
        int status = 0;
        char statusText[256];
        size_t headerStart = 0;
        if (xt_fetch_parse_status_line(raw.data, raw.length, &status, statusText, sizeof(statusText), &headerStart) == 0) {
          int stop = 0;
          if (status == 204 || status == 304 || (status >= 100 && status < 200) || strcmp(method, "HEAD") == 0) {
            stop = 1;
          } else {
            size_t contentLength = 0;
            int hasContentLength = 0;
            int chunked = 0;
            xt_fetch_scan_headers(raw.data, headerStart, (size_t)headEnd - 2, &contentLength, &hasContentLength, &chunked);
            if (chunked) {
              size_t decoded = 0;
              if (xt_fetch_chunked_complete(raw.data, raw.length, (size_t)headEnd, &decoded) == 1) stop = 1;
            } else if (hasContentLength && raw.length - (size_t)headEnd >= contentLength) {
              stop = 1;
            }
          }
          if (stop) break;
        }
      }
      int count = xt_fetch_recv(fd, chunk, sizeof(chunk));
      if (count > 0) {
        xt_fetch_buffer_append(&raw, chunk, (size_t)count);
        continue;
      }
      break;
    }
    xt_fetch_close_socket(fd);

    int headEnd = xt_fetch_head_end(&raw);
    int status = 0;
    char statusText[256];
    size_t headerStart = 0;
    if (headEnd <= 0 ||
        xt_fetch_parse_status_line(raw.data, raw.length, &status, statusText, sizeof(statusText), &headerStart) != 0) {
      xt_fetch_buffer_free(&raw);
      xt_fetch_buffer_free(&body);
      return xt_fetch_reject("fetch failed: invalid response");
    }

    xt_value responseHeaders = xt_headers_new();
    xt_headers_parse_block(responseHeaders, raw.data + headerStart, (size_t)headEnd - 2 - headerStart);

    int isRedirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    if (isRedirect && strcmp(redirectMode, "error") == 0) {
      xt_fetch_buffer_free(&raw);
      xt_fetch_buffer_free(&body);
      return xt_fetch_reject("fetch failed: redirect not allowed");
    }
    if (isRedirect && strcmp(redirectMode, "manual") != 0 && redirects < 20) {
      const char *location = xt_headers_lookup(responseHeaders, "location");
      char resolved[4096];
      if (location && xt_fetch_resolve_location(&parsed, location, resolved, sizeof(resolved)) == 0) {
        if (status == 303 || ((status == 301 || status == 302) && strcmp(method, "POST") == 0)) {
          snprintf(method, sizeof(method), "GET");
          body.length = 0;
        }
        snprintf(currentUrl, sizeof(currentUrl), "%s", resolved);
        xt_fetch_buffer_free(&raw);
        continue;
      }
    }

    size_t contentLength = 0;
    int hasContentLength = 0;
    int chunked = 0;
    xt_fetch_scan_headers(raw.data, headerStart, (size_t)headEnd - 2, &contentLength, &hasContentLength, &chunked);
    xt_fetch_buffer responseBody;
    xt_fetch_buffer_init(&responseBody);
    if (status == 204 || status == 304 || (status >= 100 && status < 200) || strcmp(method, "HEAD") == 0) {
      /* No body by definition. */
    } else if (chunked) {
      xt_fetch_decode_chunked(raw.data, raw.length, (size_t)headEnd, &responseBody);
    } else if (hasContentLength) {
      size_t available = raw.length - (size_t)headEnd;
      size_t take = contentLength < available ? contentLength : available;
      xt_fetch_buffer_append(&responseBody, raw.data + headEnd, take);
    } else {
      xt_fetch_buffer_append(&responseBody, raw.data + headEnd, raw.length - (size_t)headEnd);
    }

    xt_value response = xt_response_new(responseHeaders, status, statusText, currentUrl,
                                        responseBody.data ? responseBody.data : "", responseBody.length);
    xt_fetch_buffer_free(&responseBody);
    xt_fetch_buffer_free(&raw);
    xt_fetch_buffer_free(&body);
    return xt_promise_resolve(response);
  }

  xt_fetch_buffer_free(&body);
  return xt_fetch_reject("fetch failed: too many redirects");
}
