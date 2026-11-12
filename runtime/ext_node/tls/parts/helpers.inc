/* -- value helpers -------------------------------------------------------- */

/*
 * Options follow Node's shape: `tls.connect({ port, host, servername, ca,
 * cert, key, rejectUnauthorized, ALPNProtocols }, callback)`. PEM values may be
 * strings or Buffers, so every text option goes through {@link xt_tls_text}.
 */

static char *xt_tls_text(xt_value value) {
  if (xt_node_is_buffer(value)) {
    size_t length = 0;
    unsigned char *bytes = xt_node_buffer_bytes(value, &length);
    char *text = (char *)malloc(length + 1);
    if (text == NULL) {
      free(bytes);
      return NULL;
    }
    if (length > 0) memcpy(text, bytes, length);
    text[length] = '\0';
    free(bytes);
    return text;
  }
  const char *text = xt_string_data(xt_to_string(value));
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) return NULL;
  memcpy(copy, text, length + 1);
  return copy;
}

static char *xt_tls_option_text(xt_value options, const char *key) {
  if (!XT_IS_OBJECT(options)) return NULL;
  xt_value value = xt_object_get_cstr(options, key);
  if (xt_truthy(xt_is_nullish(value))) return NULL;
  return xt_tls_text(value);
}

static int xt_tls_option_flag(xt_value options, const char *key, int fallback) {
  if (!XT_IS_OBJECT(options)) return fallback;
  xt_value value = xt_object_get_cstr(options, key);
  if (xt_truthy(xt_is_nullish(value))) return fallback;
  return xt_truthy(value) ? 1 : 0;
}

/** Join an `ALPNProtocols` array (or a comma-separated string) into a list. */
static char *xt_tls_alpn_option(xt_value options) {
  if (!XT_IS_OBJECT(options)) return NULL;
  xt_value value = xt_object_get_cstr(options, "ALPNProtocols");
  if (xt_truthy(xt_is_nullish(value))) value = xt_object_get_cstr(options, "alpn");
  if (xt_truthy(xt_is_nullish(value))) return NULL;
  if (XT_IS_ARRAY(value)) {
    size_t capacity = 64;
    char *joined = (char *)malloc(capacity);
    if (joined == NULL) return NULL;
    size_t length = 0;
    joined[0] = '\0';
    int32_t count = (int32_t)xt_to_number(xt_array_length(value));
    for (int32_t i = 0; i < count; i++) {
      char *item = xt_tls_text(xt_array_get(value, xt_number((double)i)));
      if (item == NULL) continue;
      size_t itemLength = strlen(item);
      if (length + itemLength + 2 > capacity) {
        capacity = (length + itemLength + 2) * 2;
        char *grown = (char *)realloc(joined, capacity);
        if (grown == NULL) {
          free(item);
          free(joined);
          return NULL;
        }
        joined = grown;
      }
      if (length > 0) joined[length++] = ',';
      memcpy(joined + length, item, itemLength);
      length += itemLength;
      joined[length] = '\0';
      free(item);
    }
    return joined;
  }
  return xt_tls_text(value);
}

static unsigned char *xt_tls_bytes(xt_value value, size_t *outLength) {
  if (xt_node_is_buffer(value)) return xt_node_buffer_bytes(value, outLength);
  xt_value text = xt_to_string(value);
  const char *data = xt_string_data(text);
  size_t length = data ? (size_t)xt_string_length(xt_as_string(text)) : 0;
  unsigned char *bytes = (unsigned char *)malloc(length + 1);
  if (bytes == NULL) {
    *outLength = 0;
    return NULL;
  }
  if (length > 0) memcpy(bytes, data, length);
  bytes[length] = '\0';
  *outLength = length;
  return bytes;
}

static xt_value *xt_tls_box(xt_value value) {
  xt_value *box = (xt_value *)malloc(sizeof(xt_value));
  if (box != NULL) *box = value;
  return box;
}

static xt_value xt_tls_error_value(const char *message) {
  return xt_error_from_message(message[0] != '\0' ? message : "TLS operation failed");
}

static xt_tls_conn *xt_tls_conn_of(xt_value socket) {
  double raw = xt_to_number(xt_object_get_cstr(socket, "__conn"));
  return (xt_tls_conn *)(uintptr_t)raw;
}

static void xt_tls_set_conn(xt_value socket, xt_tls_conn *conn) {
  xt_node_set(socket, "__conn", xt_number((double)(uintptr_t)conn));
}

static int xt_tls_socket_fd(xt_value socket) {
  return (int)xt_to_number(xt_object_get_cstr(socket, "__fd"));
}
