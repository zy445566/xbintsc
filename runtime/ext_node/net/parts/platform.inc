/* -- platform helpers ----------------------------------------------------- */

static void xt_net_set_nonblocking(int fd) {
#if defined(_WIN32)
  u_long mode = 1;
  ioctlsocket(fd, FIONBIO, &mode);
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static xt_value *xt_net_box(xt_value value) {
  xt_value *handle = (xt_value *)malloc(sizeof(xt_value));
  *handle = value;
  return handle;
}

static int xt_net_fd(xt_value object) {
  return (int)xt_to_number(xt_object_get_cstr(object, "__fd"));
}

static unsigned char *xt_net_bytes(xt_value data, size_t *outLength) {
  if (xt_node_is_buffer(data)) return xt_node_buffer_bytes(data, outLength);
  xt_value text = xt_to_string(data);
  const char *string = xt_string_data(text);
  size_t length = string ? (size_t)xt_string_length(xt_as_string(text)) : 0;
  unsigned char *bytes = (unsigned char *)malloc(length + 1);
  if (length > 0 && string) memcpy(bytes, string, length);
  bytes[length] = '\0';
  *outLength = length;
  return bytes;
}

static int xt_net_send_all(int fd, const unsigned char *data, size_t length) {
  size_t sent = 0;
  while (sent < length) {
#if defined(_WIN32)
    int written = send(fd, (const char *)data + sent, (int)(length - sent), 0);
#else
    ssize_t written = send(fd, data + sent, length - sent, 0);
#endif
    if (written > 0) {
      sent += (size_t)written;
      continue;
    }
    if (written < 0 && xt_net_would_block()) {
      fd_set writeSet;
      FD_ZERO(&writeSet);
      FD_SET(fd, &writeSet);
      struct timeval timeout;
      timeout.tv_sec = 2;
      timeout.tv_usec = 0;
      if (select(fd + 1, NULL, &writeSet, NULL, &timeout) <= 0) return -1;
      continue;
    }
    return -1;
  }
  return 0;
}
