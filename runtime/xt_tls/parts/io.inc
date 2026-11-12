/* -- blocking socket BIO and stream I/O ----------------------------------- */

/*
 * Every connection this backend owns is switched to blocking mode: `fetch`
 * runs its exchange on an async worker, and the Node sockets only call
 * `read`/`write` after the reactor reported readiness. Blocking here keeps the
 * SSL state machine simple and never busy-loops.
 */

#if defined(_WIN32)
#include <winsock2.h>
typedef int xt_tls_socklen_t;
#define xt_tls_closesocket closesocket
#else
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
typedef socklen_t xt_tls_socklen_t;
#define xt_tls_closesocket close
#endif

static void xt_tls_set_blocking(int fd) {
#if defined(_WIN32)
  u_long mode = 0;
  ioctlsocket(fd, FIONBIO, &mode);
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
#endif
}

int xt_tls_send_bio(void *ctx, const unsigned char *buf, size_t len) {
  xt_tls_conn *conn = (xt_tls_conn *)ctx;
  size_t sent = 0;
  while (sent < len) {
#if defined(_WIN32)
    int written = send(conn->fd, (const char *)buf + sent, (int)(len - sent), 0);
#else
    ssize_t written = send(conn->fd, buf + sent, len - sent, 0);
#endif
    if (written > 0) {
      sent += (size_t)written;
      continue;
    }
    if (written < 0) return MBEDTLS_ERR_NET_SEND_FAILED;
    return MBEDTLS_ERR_SSL_WANT_WRITE;
  }
  return (int)sent;
}

int xt_tls_recv_bio(void *ctx, unsigned char *buf, size_t len) {
  xt_tls_conn *conn = (xt_tls_conn *)ctx;
#if defined(_WIN32)
  int count = recv(conn->fd, (char *)buf, (int)len, 0);
#else
  ssize_t count = recv(conn->fd, buf, len, 0);
#endif
  if (count < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
  return (int)count;
}

static int xt_tls_conn_read(xt_tls_conn *conn, unsigned char *buffer, size_t capacity) {
  if (conn == NULL) return -1;
  for (;;) {
    int ret = mbedtls_ssl_read(&conn->ssl, buffer, capacity);
    if (ret > 0) return ret;
    if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
    xt_tls_conn_fail(conn, "TLS read failed", ret);
    return -1;
  }
}

static int xt_tls_conn_write(xt_tls_conn *conn, const unsigned char *data, size_t length) {
  if (conn == NULL) return -1;
  size_t sent = 0;
  while (sent < length) {
    int ret = mbedtls_ssl_write(&conn->ssl, data + sent, length - sent);
    if (ret > 0) {
      sent += (size_t)ret;
      continue;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
    xt_tls_conn_fail(conn, "TLS write failed", ret);
    return -1;
  }
  return 0;
}

static void xt_tls_conn_shutdown(xt_tls_conn *conn) {
  if (conn == NULL) return;
  /* Best effort: the peer may already be gone. */
  (void)mbedtls_ssl_close_notify(&conn->ssl);
}

void xt_tls_conn_free(xt_tls_conn *conn) {
  if (conn == NULL) return;
  mbedtls_ssl_free(&conn->ssl);
  if (conn->owns_setup && conn->setup != NULL) {
    xt_tls_setup_free(conn->setup);
    free(conn->setup);
  }
  free(conn);
}

static void xt_tls_conn_close(xt_tls_conn *conn) {
  if (conn == NULL) return;
  xt_tls_conn_shutdown(conn);
  if (conn->fd >= 0) xt_tls_closesocket(conn->fd);
  conn->fd = -1;
  xt_tls_conn_free(conn);
}

static const char *xt_tls_conn_alpn(xt_tls_conn *conn) {
  if (conn == NULL) return NULL;
  const char *protocol = mbedtls_ssl_get_alpn_protocol(&conn->ssl);
  if (protocol == NULL) return NULL;
  snprintf(conn->alpn, sizeof(conn->alpn), "%s", protocol);
  return conn->alpn;
}

static const char *xt_tls_conn_error(xt_tls_conn *conn) {
  return conn == NULL ? "TLS connection is not available" : conn->error;
}

static int xt_tls_conn_fd(xt_tls_conn *conn) { return conn == NULL ? -1 : conn->fd; }

static int xt_tls_finish_handshake(xt_tls_conn *conn, int verify) {
  int ret;
  while ((ret = mbedtls_ssl_handshake(&conn->ssl)) != 0) {
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
    /* A certificate failure aborts the handshake itself; report the specific
     * reason (expired, wrong host, untrusted root, ...) instead of a generic
     * X509 error. */
    uint32_t flags = mbedtls_ssl_get_verify_result(&conn->ssl);
    if (flags != 0) {
      char info[256];
      mbedtls_x509_crt_verify_info(info, sizeof(info), "", flags);
      snprintf(conn->error, sizeof(conn->error), "certificate verification failed: %s", info);
    } else {
      xt_tls_conn_fail(conn, "TLS handshake failed", ret);
    }
    return -1;
  }
  if (verify) {
    uint32_t flags = mbedtls_ssl_get_verify_result(&conn->ssl);
    if (flags != 0) {
      char info[256];
      mbedtls_x509_crt_verify_info(info, sizeof(info), "", flags);
      snprintf(conn->error, sizeof(conn->error), "certificate verification failed: %s", info);
      return -1;
    }
  }
  xt_tls_conn_alpn(conn);
  return 0;
}
