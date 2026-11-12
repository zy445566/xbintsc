/* -- TCP socket helpers --------------------------------------------------- */

/*
 * `fetch` performs a conventional blocking request/response exchange on an
 * async I/O worker: connect, write the request, read until the response is
 * complete (or the peer closes). The socket carries a receive timeout so a
 * stalled server cannot pin a worker forever; the timeout surfaces as a
 * rejected promise.
 */
#define XT_FETCH_TIMEOUT_SECONDS 30

static void xt_fetch_set_timeouts(int fd) {
#if defined(_WIN32)
  DWORD ms = (DWORD)XT_FETCH_TIMEOUT_SECONDS * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, (int)sizeof(ms));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, (int)sizeof(ms));
#else
  struct timeval timeout;
  timeout.tv_sec = XT_FETCH_TIMEOUT_SECONDS;
  timeout.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

static int xt_fetch_connect(const xt_fetch_url *url) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *result = NULL;
  if (getaddrinfo(url->host, url->port, &hints, &result) != 0) return -1;
  int fd = -1;
  for (struct addrinfo *info = result; info; info = info->ai_next) {
    int candidate = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (candidate < 0) continue;
    if (connect(candidate, info->ai_addr, (xt_fetch_socklen_t)info->ai_addrlen) == 0) {
      fd = candidate;
      break;
    }
    xt_fetch_close_socket(candidate);
  }
  freeaddrinfo(result);
  if (fd >= 0) xt_fetch_set_timeouts(fd);
  return fd;
}

static int xt_fetch_send_all(int fd, const char *data, size_t length) {
  size_t sent = 0;
  while (sent < length) {
#if defined(_WIN32)
    int written = send(fd, data + sent, (int)(length - sent), 0);
#else
    ssize_t written = send(fd, data + sent, length - sent, 0);
#endif
    if (written > 0) {
      sent += (size_t)written;
      continue;
    }
    return -1;
  }
  return 0;
}

/** Receive one chunk; returns the byte count, `0` on close, `-1` on error. */
static int xt_fetch_recv(int fd, char *buffer, size_t capacity) {
#if defined(_WIN32)
  return recv(fd, buffer, (int)capacity, 0);
#else
  ssize_t count = recv(fd, buffer, capacity, 0);
  return count > 2147483647 ? 2147483647 : (int)count;
#endif
}

/* -- transport abstraction (plain TCP or TLS) ------------------------------ */

/*
 * The worker is the same for both schemes: only connect/write/read/close
 * differ. TLS is reached through the optional `tls` link feature's dispatch
 * table, which is NULL in a program that never enables it; an `https://`
 * request then rejects instead of failing to link.
 */
typedef struct {
  int fd;
  xt_tls_conn *tls;
} xt_fetch_stream;

static int xt_fetch_stream_open(const xt_fetch_url *url, xt_fetch_stream *stream, char *err,
                                size_t errlen) {
  stream->fd = -1;
  stream->tls = NULL;
  if (!url->secure) {
    stream->fd = xt_fetch_connect(url);
    return stream->fd < 0 ? -1 : 0;
  }
  if (xt_tls_core_api == NULL) {
    snprintf(err, errlen, "https is not supported (the tls link feature is not linked)");
    return -1;
  }
  int port = url->port[0] != '\0' ? atoi(url->port) : 443;
  stream->tls = xt_tls_core_api->client_connect(url->host, port, url->host, 1, NULL, NULL, NULL,
                                                "http/1.1", err, errlen);
  if (stream->tls == NULL) return -1;
  stream->fd = xt_tls_core_api->fd(stream->tls);
  return 0;
}

static int xt_fetch_stream_send(xt_fetch_stream *stream, const char *data, size_t length) {
  if (stream->tls != NULL) return xt_tls_core_api->write(stream->tls, (const unsigned char *)data, length);
  return xt_fetch_send_all(stream->fd, data, length);
}

static int xt_fetch_stream_recv(xt_fetch_stream *stream, char *buffer, size_t capacity) {
  if (stream->tls != NULL) {
    return xt_tls_core_api->read(stream->tls, (unsigned char *)buffer, capacity);
  }
  return xt_fetch_recv(stream->fd, buffer, capacity);
}

static void xt_fetch_stream_close(xt_fetch_stream *stream) {
  if (stream->tls != NULL) {
    xt_tls_core_api->close(stream->tls);
    stream->tls = NULL;
  } else if (stream->fd >= 0) {
    xt_fetch_close_socket(stream->fd);
  }
  stream->fd = -1;
}
