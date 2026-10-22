/* -- asynchronous request/response exchange ------------------------------- */

/*
 * `fetch` is a state machine over the async I/O pool. The main thread validates
 * the URL, turns `init.headers` into a `Headers` object and serialises the
 * request; a worker performs the blocking `getaddrinfo`/connect/send/recv
 * exchange; the main thread parses the raw response, follows redirects and
 * builds the `Response`. Nothing observed from JavaScript blocks the reactor, so
 * timers and sockets keep running while a request is in flight.
 *
 * The worker only sees plain C memory. The values that must survive across the
 * suspension (`promise`, `headers`) are held in the job's `roots` slots and
 * scanned by the collector.
 */

typedef struct xt_fetch_state xt_fetch_state;

typedef struct {
  xt_async_job base;
  xt_fetch_state *state;
  char *request;
  size_t request_length;
  xt_fetch_buffer raw;
  int failed;
} xt_fetch_hop;

struct xt_fetch_state {
  xt_value promise;
  xt_value headers_source;
  char *url;
  char method[16];
  char redirect[16];
  xt_fetch_buffer body;
  int redirects;
};

static void xt_fetch_start(xt_fetch_state *state);

static void xt_fetch_state_free(xt_fetch_state *state) {
  if (!state) return;
  free(state->url);
  xt_fetch_buffer_free(&state->body);
  free(state);
}

static void xt_fetch_state_reject(xt_fetch_state *state, const char *message) {
  xt_value argument = xt_string_from_cstr(message);
  xt_value error = xt_type_error_ctor(1, &argument);
  xt_value promise = state->promise;
  xt_fetch_state_free(state);
  xt_promise_reject_value(promise, error);
}

static void xt_fetch_hop_release(xt_async_job *job) {
  xt_fetch_hop *hop = (xt_fetch_hop *)job;
  free(hop->request);
  xt_fetch_buffer_free(&hop->raw);
  free(hop);
}

/* Worker thread: connect, write the request, read until the response is
 * complete (or the peer closes). Only plain C memory is touched here. */
static void xt_fetch_hop_run(xt_async_job *job) {
  xt_fetch_hop *hop = (xt_fetch_hop *)job;
  xt_fetch_url parsed;
  if (xt_fetch_parse_url(hop->state->url, &parsed) != 0) {
    hop->failed = 1;
    return;
  }
  int fd = xt_fetch_connect(&parsed);
  if (fd < 0) {
    hop->failed = 1;
    return;
  }
  if (xt_fetch_send_all(fd, hop->request, hop->request_length) != 0) {
    xt_fetch_close_socket(fd);
    hop->failed = 1;
    return;
  }

  char chunk[16384];
  for (;;) {
    int headEnd = xt_fetch_head_end(&hop->raw);
    if (headEnd > 0) {
      int status = 0;
      char statusText[256];
      size_t headerStart = 0;
      if (xt_fetch_parse_status_line(hop->raw.data, hop->raw.length, &status, statusText, sizeof(statusText),
                                     &headerStart) == 0) {
        int stop = 0;
        if (status == 204 || status == 304 || (status >= 100 && status < 200) ||
            strcmp(hop->state->method, "HEAD") == 0) {
          stop = 1;
        } else {
          size_t contentLength = 0;
          int hasContentLength = 0;
          int chunked = 0;
          xt_fetch_scan_headers(hop->raw.data, headerStart, (size_t)headEnd - 2, &contentLength, &hasContentLength,
                                &chunked);
          if (chunked) {
            size_t decoded = 0;
            if (xt_fetch_chunked_complete(hop->raw.data, hop->raw.length, (size_t)headEnd, &decoded) == 1) stop = 1;
          } else if (hasContentLength && hop->raw.length - (size_t)headEnd >= contentLength) {
            stop = 1;
          }
        }
        if (stop) break;
      }
    }
    int count = xt_fetch_recv(fd, chunk, sizeof(chunk));
    if (count > 0) {
      xt_fetch_buffer_append(&hop->raw, chunk, (size_t)count);
      continue;
    }
    break;
  }
  xt_fetch_close_socket(fd);
}

#define XT_FETCH_HOP(c) ((xt_fetch_hop *)(c))

/* Main thread: parse the raw response and either follow a redirect (starting a
 * fresh hop) or build the `Response` and settle the promise. */
static void xt_fetch_hop_complete(xt_async_job *job) {
  xt_fetch_hop *hop = XT_FETCH_HOP(job);
  xt_fetch_state *state = hop->state;
  if (hop->failed) {
    xt_fetch_state_reject(state, "fetch failed");
    return;
  }

  xt_fetch_buffer *raw = &hop->raw;
  int headEnd = xt_fetch_head_end(raw);
  int status = 0;
  char statusText[256];
  size_t headerStart = 0;
  if (headEnd <= 0 ||
      xt_fetch_parse_status_line(raw->data, raw->length, &status, statusText, sizeof(statusText), &headerStart) != 0) {
    xt_fetch_state_reject(state, "fetch failed: invalid response");
    return;
  }

  xt_value responseHeaders = xt_headers_new();
  xt_headers_parse_block(responseHeaders, raw->data + headerStart, (size_t)headEnd - 2 - headerStart);

  int isRedirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
  if (isRedirect && strcmp(state->redirect, "error") == 0) {
    xt_fetch_state_reject(state, "fetch failed: redirect not allowed");
    return;
  }
  if (isRedirect && strcmp(state->redirect, "manual") != 0 && state->redirects < 20) {
    xt_fetch_url parsed;
    if (xt_fetch_parse_url(state->url, &parsed) == 0) {
      const char *location = xt_headers_lookup(responseHeaders, "location");
      char resolved[4096];
      if (location && xt_fetch_resolve_location(&parsed, location, resolved, sizeof(resolved)) == 0) {
        if (status == 303 || ((status == 301 || status == 302) && strcmp(state->method, "POST") == 0)) {
          snprintf(state->method, sizeof(state->method), "GET");
          state->body.length = 0;
        }
        free(state->url);
        size_t length = strlen(resolved);
        state->url = (char *)malloc(length + 1);
        if (!state->url) {
          xt_fetch_state_reject(state, "fetch failed");
          return;
        }
        memcpy(state->url, resolved, length + 1);
        state->redirects++;
        xt_fetch_start(state);
        return;
      }
    }
  }

  size_t contentLength = 0;
  int hasContentLength = 0;
  int chunked = 0;
  xt_fetch_scan_headers(raw->data, headerStart, (size_t)headEnd - 2, &contentLength, &hasContentLength, &chunked);
  xt_fetch_buffer responseBody;
  xt_fetch_buffer_init(&responseBody);
  if (status == 204 || status == 304 || (status >= 100 && status < 200) || strcmp(state->method, "HEAD") == 0) {
    /* No body by definition. */
  } else if (chunked) {
    xt_fetch_decode_chunked(raw->data, raw->length, (size_t)headEnd, &responseBody);
  } else if (hasContentLength) {
    size_t available = raw->length - (size_t)headEnd;
    size_t take = contentLength < available ? contentLength : available;
    xt_fetch_buffer_append(&responseBody, raw->data + headEnd, take);
  } else {
    xt_fetch_buffer_append(&responseBody, raw->data + headEnd, raw->length - (size_t)headEnd);
  }

  xt_value response = xt_response_new(responseHeaders, status, statusText, state->url,
                                      responseBody.data ? responseBody.data : "", responseBody.length);
  xt_fetch_buffer_free(&responseBody);
  xt_value promise = state->promise;
  xt_fetch_state_free(state);
  xt_promise_fulfill(promise, response);
}

/* Main thread: validate the current URL and queue one exchange. */
static void xt_fetch_start(xt_fetch_state *state) {
  xt_fetch_url parsed;
  if (xt_fetch_parse_url(state->url, &parsed) != 0) {
    xt_fetch_state_reject(state, "Failed to parse URL");
    return;
  }
  if (parsed.secure) {
    xt_fetch_state_reject(state, "fetch failed: https is not supported");
    return;
  }

  xt_value requestHeaders = xt_headers_from_source(state->headers_source);
  xt_fetch_buffer request;
  xt_fetch_buffer_init(&request);
  xt_fetch_build_request(state->method, &parsed, requestHeaders, state->body.length > 0 ? &state->body : NULL,
                         &request);

  xt_fetch_hop *hop = (xt_fetch_hop *)calloc(1, sizeof(xt_fetch_hop));
  if (!hop) abort();
  hop->base.roots[0] = state->promise;
  hop->base.roots[1] = state->headers_source;
  hop->base.run = xt_fetch_hop_run;
  hop->base.complete = xt_fetch_hop_complete;
  hop->base.release = xt_fetch_hop_release;
  hop->state = state;
  hop->request = request.data;
  hop->request_length = request.length;
  xt_fetch_buffer_init(&hop->raw);
  xt_async_submit(&hop->base);
}

/** Implements the global `fetch(input, init)`. The returned promise settles
 * once a worker has completed the exchange and the reactor dispatches it. */
xt_value xt_fetch(int32_t argc, xt_value *argv) {
  xt_value input = xt_arg_at(argc, argv, 0);
  xt_value init = xt_arg_at(argc, argv, 1);
  if (input == XT_UNDEFINED || input == XT_NULL) {
    xt_value argument = xt_string_from_cstr("Failed to parse URL from undefined");
    return xt_promise_reject(xt_type_error_ctor(1, &argument));
  }

  xt_value urlValue = input;
  if (XT_IS_OBJECT(input)) {
    xt_value candidate = xt_object_get_cstr(input, "url");
    if (XT_IS_STRING(candidate)) urlValue = candidate;
  }
  const char *urlText = xt_string_data(xt_to_string(urlValue));
  if (!urlText) {
    xt_value argument = xt_string_from_cstr("Failed to parse URL");
    return xt_promise_reject(xt_type_error_ctor(1, &argument));
  }

  xt_fetch_state *state = (xt_fetch_state *)calloc(1, sizeof(xt_fetch_state));
  if (!state) abort();
  size_t urlLength = strlen(urlText);
  state->url = (char *)malloc(urlLength + 1);
  if (!state->url) abort();
  memcpy(state->url, urlText, urlLength + 1);
  snprintf(state->method, sizeof(state->method), "GET");
  snprintf(state->redirect, sizeof(state->redirect), "follow");
  xt_fetch_buffer_init(&state->body);
  state->headers_source = XT_UNDEFINED;
  state->redirects = 0;

  if (XT_IS_OBJECT(init)) {
    xt_value methodValue = xt_object_get_cstr(init, "method");
    if (XT_IS_STRING(methodValue)) {
      const char *text = xt_string_data(methodValue);
      size_t i = 0;
      for (; text && text[i] && i + 1 < sizeof(state->method); i++) {
        char c = text[i];
        state->method[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
      }
      state->method[i] = '\0';
    }
    state->headers_source = xt_object_get_cstr(init, "headers");
    xt_value bodySource = xt_object_get_cstr(init, "body");
    if (bodySource != XT_UNDEFINED && bodySource != XT_NULL) {
      xt_value text = xt_to_string(bodySource);
      const char *data = xt_string_data(text);
      if (data) xt_fetch_buffer_append(&state->body, data, (size_t)xt_string_length_value(text));
    }
    xt_value redirectValue = xt_object_get_cstr(init, "redirect");
    if (XT_IS_STRING(redirectValue)) {
      snprintf(state->redirect, sizeof(state->redirect), "%s", xt_string_data(redirectValue));
    }
  }

  state->promise = xt_promise_create();
  xt_value promise = state->promise;
  xt_fetch_start(state);
  return promise;
}
