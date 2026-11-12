/* -- shared HTTP/2 connection state --------------------------------------- */

/*
 * nghttp2 owns the protocol state machine (framing, HPACK, flow control) and
 * calls back into this file for every header/data/close event. One
 * `xt_h2_conn` bridges a socket (plain or TLS) to one nghttp2 session and to
 * the JavaScript session object, which holds the stream table so callbacks can
 * find the object a stream id belongs to.
 */

#include "../../../xt_http2/nghttp2_static.h"

#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#define xt_h2_closesocket closesocket
#else
#include <sys/socket.h>
#include <unistd.h>
#define xt_h2_closesocket close
#endif

typedef struct xt_h2_conn xt_h2_conn;

/* Outgoing request/response body; nghttp2 pulls from it as flow-control credit
 * becomes available. `source.ptr` in the data provider points here. */
typedef struct {
  unsigned char *data;
  size_t length;
  size_t sent;
  int ended;
} xt_h2_body;

struct xt_h2_conn {
  nghttp2_session *session;
  int fd;
  xt_tls_conn *tls;
  xt_value session_value;
  xt_value owner;
  int is_server;
  int closed;
  int in_recv;
  int in_send;
  int flush_pending;
  int finish_requested;
  char scheme[8];
  char authority[512];
};

/* Byte helpers exported by buffer.c (linked through the node extension). */
xt_value xt_buffer_ctor(int32_t argc, xt_value *argv);
void xt_node_buffer_set(xt_value value, uint32_t index, unsigned char byte);

static xt_h2_conn *xt_h2_conn_of(xt_value object) {
  return (xt_h2_conn *)(uintptr_t)xt_to_number(xt_object_get_cstr(object, "__h2"));
}

static xt_value xt_h2_error(const char *message) {
  return xt_error_from_message(message != NULL && message[0] != '\0' ? message : "HTTP/2 error");
}

static xt_value xt_h2_buffer(const unsigned char *data, size_t length) {
  xt_value size = xt_number((double)length);
  xt_value buffer = xt_buffer_ctor(1, &size);
  for (size_t i = 0; i < length; i++) xt_node_buffer_set(buffer, (uint32_t)i, data[i]);
  return buffer;
}

static xt_h2_body *xt_h2_body_new(void) {
  xt_h2_body *body = (xt_h2_body *)calloc(1, sizeof(xt_h2_body));
  return body;
}

static void xt_h2_body_append(xt_h2_body *body, const unsigned char *data, size_t length) {
  if (body == NULL || length == 0) return;
  unsigned char *grown = (unsigned char *)realloc(body->data, body->length + length);
  if (grown == NULL) return;
  body->data = grown;
  memcpy(body->data + body->length, data, length);
  body->length += length;
}

/** The body pointer stored on a JS object, or NULL for "no body". */
static xt_h2_body *xt_h2_body_of(xt_value object) {
  xt_value value = xt_object_get_cstr(object, "__body");
  if (!XT_IS_NUMBER(value)) return NULL;
  double raw = xt_to_number(value);
  if (!(raw > 0)) return NULL;
  return (xt_h2_body *)(uintptr_t)raw;
}

static void xt_h2_body_free(xt_h2_body *body) {
  if (body == NULL) return;
  free(body->data);
  free(body);
}

static int xt_h2_write_all(xt_h2_conn *conn, const unsigned char *data, size_t length) {
  if (conn->tls != NULL) return xt_tls_core_api->write(conn->tls, data, length);
  size_t sent = 0;
  while (sent < length) {
#if defined(_WIN32)
    int written = send(conn->fd, (const char *)data + sent, (int)(length - sent), 0);
#else
    ssize_t written = send(conn->fd, data + sent, length - sent, 0);
#endif
    if (written <= 0) return -1;
    sent += (size_t)written;
  }
  return 0;
}

static int xt_h2_recv_some(xt_h2_conn *conn, unsigned char *buffer, size_t capacity) {
  if (conn->tls != NULL) return xt_tls_core_api->read(conn->tls, buffer, capacity);
#if defined(_WIN32)
  return recv(conn->fd, (char *)buffer, (int)capacity, 0);
#else
  ssize_t count = recv(conn->fd, buffer, capacity, 0);
  return count > 2147483647 ? 2147483647 : (int)count;
#endif
}

/** Free every request/response body once nghttp2 can no longer reference them. */
static void xt_h2_release_bodies(xt_h2_conn *conn) {
  xt_value streams = xt_object_get_cstr(conn->session_value, "__streams");
  if (!XT_IS_ARRAY(streams)) return;
  int32_t count = (int32_t)xt_to_number(xt_array_length(streams));
  for (int32_t i = 0; i < count; i++) {
    xt_value stream = xt_array_get(streams, xt_number((double)i));
    if (!XT_IS_OBJECT(stream)) continue;
    xt_h2_body *body = xt_h2_body_of(stream);
    if (body != NULL) xt_h2_body_free(body);
    xt_node_set(stream, "__body", xt_number(0));
    xt_value response = xt_object_get_cstr(stream, "__res");
    if (XT_IS_OBJECT(response)) {
      xt_h2_body *responseBody = xt_h2_body_of(response);
      if (responseBody != NULL) xt_h2_body_free(responseBody);
      xt_node_set(response, "__body", xt_number(0));
    }
  }
}

static void xt_h2_finish(xt_h2_conn *conn, const char *error) {
  if (conn == NULL || conn->closed) return;
  /* A JavaScript listener can call `session.close()` from a nghttp2 callback
   * (`end`/`close` fire there). Deleting the session while `mem_recv2`/
   * `mem_send2` is still running would free its state under the library, so the
   * teardown is deferred until the current call returns. */
  if (conn->in_recv || conn->in_send) {
    conn->finish_requested = 1;
    return;
  }
  conn->closed = 1;
  if (conn->fd >= 0) xt_loop_remove(conn->fd);
  if (conn->session != NULL) {
    nghttp2_session_del(conn->session);
    conn->session = NULL;
  }
  /* Only now is it safe: nghttp2's data providers referenced these buffers. */
  xt_h2_release_bodies(conn);
  if (conn->tls != NULL) {
    xt_tls_core_api->close(conn->tls);
    conn->tls = NULL;
  } else if (conn->fd >= 0) {
    xt_h2_closesocket(conn->fd);
  }
  conn->fd = -1;
  xt_node_set(conn->session_value, "__closed", xt_bool(1));
  if (error != NULL) xt_node_emit1(conn->session_value, "error", xt_h2_error(error));
  xt_node_emit0(conn->session_value, "close");
}

static int xt_h2_flush(xt_h2_conn *conn) {
  if (conn == NULL || conn->session == NULL || conn->closed) return -1;
  if (conn->in_recv) {
    conn->flush_pending = 1;
    return 0;
  }
  for (;;) {
    if (conn->finish_requested) break;
    const uint8_t *data = NULL;
    conn->in_send = 1;
    nghttp2_ssize length = nghttp2_session_mem_send2(conn->session, &data);
    conn->in_send = 0;
    if (length < 0) {
      xt_h2_finish(conn, "HTTP/2 send failed");
      return -1;
    }
    if (length == 0) break;
    if (xt_h2_write_all(conn, data, (size_t)length) != 0) {
      xt_h2_finish(conn, "HTTP/2 connection lost");
      return -1;
    }
  }
  if (conn->finish_requested) {
    xt_h2_finish(conn, NULL);
    return -1;
  }
  if (!nghttp2_session_want_read(conn->session) && !nghttp2_session_want_write(conn->session)) {
    xt_h2_finish(conn, NULL);
    return -1;
  }
  return 0;
}

static void xt_h2_on_readable(void *userdata, int events) {
  (void)events;
  xt_h2_conn *conn = (xt_h2_conn *)userdata;
  if (conn == NULL || conn->closed) return;
  unsigned char buffer[16384];
  int count = xt_h2_recv_some(conn, buffer, sizeof(buffer));
  if (count > 0) {
    conn->in_recv = 1;
    nghttp2_ssize consumed = nghttp2_session_mem_recv2(conn->session, buffer, (size_t)count);
    conn->in_recv = 0;
    if (conn->finish_requested) {
      xt_h2_finish(conn, NULL);
      return;
    }
    if (consumed < 0) {
      xt_h2_finish(conn, "HTTP/2 protocol error");
      return;
    }
    if (conn->flush_pending) {
      conn->flush_pending = 0;
      xt_h2_flush(conn);
    }
    return;
  }
  if (count == 0) {
    xt_h2_finish(conn, NULL);
    return;
  }
  xt_h2_finish(conn, "HTTP/2 connection lost");
}

static void xt_h2_attach(xt_h2_conn *conn, xt_value session) {
  conn->session_value = session;
  /* The session object is the anchor for the stream table and the event target
   * for callbacks; keep the C-side slot alive even if JavaScript drops its last
   * reference while I/O is pending. The connection box is never freed, so the
   * root slot stays valid for the process lifetime. */
  xt_gc_add_root(&conn->session_value);
  if (conn->fd >= 0) xt_loop_add(conn->fd, XT_IO_READ, xt_h2_on_readable, conn);
  xt_h2_flush(conn);
}

/* -- stream table --------------------------------------------------------- */

static xt_value xt_h2_stream_at(xt_h2_conn *conn, int32_t id) {
  xt_value streams = xt_object_get_cstr(conn->session_value, "__streams");
  if (!XT_IS_ARRAY(streams)) return XT_UNDEFINED;
  return xt_array_get(streams, xt_number((double)id));
}

static void xt_h2_stream_put(xt_h2_conn *conn, int32_t id, xt_value stream) {
  xt_value streams = xt_object_get_cstr(conn->session_value, "__streams");
  if (XT_IS_ARRAY(streams)) xt_array_set(streams, xt_number((double)id), stream);
}

/* -- header object -------------------------------------------------------- */

static void xt_h2_header_add(xt_value headers, const uint8_t *name, size_t nameLength,
                             const uint8_t *value, size_t valueLength) {
  char key[256];
  size_t keyLength = nameLength < sizeof(key) - 1 ? nameLength : sizeof(key) - 1;
  memcpy(key, name, keyLength);
  key[keyLength] = '\0';
  xt_value keyValue = xt_string_from_cstr(key);
  xt_value text = xt_string_new((const char *)value, valueLength);
  xt_value existing = xt_object_get(headers, keyValue);
  if (XT_IS_STRING(existing)) {
    const char *previous = xt_string_data(existing);
    char *combined = (char *)malloc(strlen(previous) + valueLength + 3);
    if (combined != NULL) {
      sprintf(combined, "%s, ", previous);
      memcpy(combined + strlen(previous) + 2, value, valueLength);
      combined[strlen(previous) + 2 + valueLength] = '\0';
      xt_value merged = xt_string_from_cstr(combined);
      free(combined);
      xt_object_set(headers, keyValue, merged);
      return;
    }
  }
  xt_object_set(headers, keyValue, text);
}

/* -- URL / authority ------------------------------------------------------ */

static int xt_h2_parse_authority(const char *url, char *scheme, size_t schemeSize, char *host,
                                 size_t hostSize, int *port, char *authority, size_t authoritySize) {
  if (url == NULL) return -1;
  const char *cursor = url;
  int secure = 0;
  if (strncmp(cursor, "https://", 8) == 0) {
    secure = 1;
    cursor += 8;
    snprintf(scheme, schemeSize, "https");
    *port = 443;
  } else if (strncmp(cursor, "http://", 7) == 0) {
    cursor += 7;
    snprintf(scheme, schemeSize, "http");
    *port = 80;
  } else {
    return -1;
  }
  const char *end = cursor;
  while (*end != '\0' && *end != '/' && *end != '?' && *end != '#') end++;
  const char *hostStart = cursor;
  for (const char *p = cursor; p < end; p++) {
    if (*p == '@') hostStart = p + 1;
  }
  const char *hostEnd = end;
  const char *colon = memchr(hostStart, ':', (size_t)(end - hostStart));
  if (colon != NULL) {
    hostEnd = colon;
    *port = atoi(colon + 1);
  }
  size_t hostLength = (size_t)(hostEnd - hostStart);
  if (hostLength == 0 || hostLength >= hostSize) return -1;
  memcpy(host, hostStart, hostLength);
  host[hostLength] = '\0';
  size_t authorityLength = (size_t)(end - cursor);
  if (authorityLength >= authoritySize) return -1;
  memcpy(authority, cursor, authorityLength);
  authority[authorityLength] = '\0';
  return secure;
}
