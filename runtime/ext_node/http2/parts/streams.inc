/* -- JavaScript objects: sessions, streams, requests, responses ----------- */

#define XT_H2_MAX_HEADERS 64

static xt_value xt_h2_client_session_proto(void);
static xt_value xt_h2_stream_proto(void);
static xt_value xt_h2_request_proto(void);
static xt_value xt_h2_response_proto(void);

static xt_value xt_h2_session_request(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_session_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_stream_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_stream_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_stream_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_stream_set_encoding(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_request_respond(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_request_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_request_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_request_set_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_request_get_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_request_get_headers(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_response_write_head(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_response_set_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_response_get_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_response_get_headers(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_response_remove_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_response_has_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_response_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_response_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_response_flush(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_h2_response_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);

static xt_value xt_h2_noop(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return thisValue;
}

/* -- header vector -------------------------------------------------------- */

static char *xt_h2_lowercase(const char *name) {
  size_t length = strlen(name);
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) return NULL;
  for (size_t i = 0; i < length; i++) {
    char c = name[i];
    copy[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
  copy[length] = '\0';
  return copy;
}

static size_t xt_h2_add_nv(nghttp2_nv *nva, size_t count, size_t max, const char *name,
                           const char *value) {
  if (count >= max || name == NULL || name[0] == '\0') return count;
  size_t nameLength = strlen(name);
  size_t valueLength = value != NULL ? strlen(value) : 0;
  uint8_t *nameCopy = (uint8_t *)malloc(nameLength);
  uint8_t *valueCopy = (uint8_t *)malloc(valueLength > 0 ? valueLength : 1);
  if (nameCopy == NULL || valueCopy == NULL) {
    free(nameCopy);
    free(valueCopy);
    return count;
  }
  memcpy(nameCopy, name, nameLength);
  if (valueLength > 0) memcpy(valueCopy, value, valueLength);
  nva[count].name = nameCopy;
  nva[count].namelen = nameLength;
  nva[count].value = valueCopy;
  nva[count].valuelen = valueLength;
  nva[count].flags = NGHTTP2_NV_FLAG_NONE;
  return count + 1;
}

static void xt_h2_free_nv(nghttp2_nv *nva, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(nva[i].name);
    free(nva[i].value);
  }
}

/** Append every field of a JS header object (array values expand). */
static size_t xt_h2_add_headers(nghttp2_nv *nva, size_t count, size_t max, xt_value headers) {
  if (!XT_IS_OBJECT(headers)) return count;
  xt_value keys = xt_object_keys(headers);
  int32_t keyCount = (int32_t)xt_to_number(xt_array_length(keys));
  for (int32_t i = 0; i < keyCount && count < max; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    const char *name = xt_string_data(xt_to_string(key));
    if (name == NULL || strcmp(name, "__proto__") == 0) continue;
    char *lower = xt_h2_lowercase(name);
    if (lower == NULL) continue;
    if (lower[0] == ':') {
      free(lower);
      continue;
    }
    xt_value value = xt_object_get(headers, key);
    if (XT_IS_ARRAY(value)) {
      int32_t values = (int32_t)xt_to_number(xt_array_length(value));
      for (int32_t k = 0; k < values && count < max; k++) {
        const char *text = xt_string_data(xt_to_string(xt_array_get(value, xt_number((double)k))));
        if (text != NULL) count = xt_h2_add_nv(nva, count, max, lower, text);
      }
    } else if (!xt_truthy(xt_is_nullish(value))) {
      const char *text = xt_string_data(xt_to_string(value));
      if (text != NULL) count = xt_h2_add_nv(nva, count, max, lower, text);
    }
    free(lower);
  }
  return count;
}

/* -- bodies --------------------------------------------------------------- */

static nghttp2_ssize xt_h2_read_body(nghttp2_session *session, int32_t stream_id, uint8_t *buf,
                                     size_t length, uint32_t *data_flags,
                                     nghttp2_data_source *source, void *user_data) {
  (void)session;
  (void)stream_id;
  (void)user_data;
  xt_h2_body *body = (xt_h2_body *)source->ptr;
  if (body == NULL) {
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    return 0;
  }
  if (body->sent < body->length) {
    size_t remaining = body->length - body->sent;
    size_t take = remaining < length ? remaining : length;
    memcpy(buf, body->data + body->sent, take);
    body->sent += take;
    return (nghttp2_ssize)take;
  }
  if (body->ended) {
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    return 0;
  }
  return NGHTTP2_ERR_DEFERRED;
}

static void xt_h2_stream_append(xt_value stream, xt_value chunk) {
  xt_h2_body *body = xt_h2_body_of(stream);
  if (body == NULL) return;
  if (xt_node_is_buffer(chunk)) {
    size_t byteLength = 0;
    unsigned char *data = xt_node_buffer_bytes(chunk, &byteLength);
    xt_h2_body_append(body, data, byteLength);
    free(data);
    return;
  }
  const char *text = xt_string_data(xt_to_string(chunk));
  if (text == NULL) return;
  xt_h2_body_append(body, (const unsigned char *)text, strlen(text));
}

/* -- protos --------------------------------------------------------------- */

static xt_value xt_h2_client_session_proto(void) {
  static xt_value proto = 0;
  xt_gc_add_root(&proto);
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "request", (void *)xt_h2_session_request);
  xt_node_define_method(proto, "close", (void *)xt_h2_session_close);
  xt_node_define_method(proto, "destroy", (void *)xt_h2_session_close);
  xt_node_define_method(proto, "setTimeout", (void *)xt_h2_noop);
  xt_node_define_method(proto, "ping", (void *)xt_h2_noop);
  xt_node_define_method(proto, "goaway", (void *)xt_h2_session_close);
  return proto;
}

static xt_value xt_h2_stream_proto(void) {
  static xt_value proto = 0;
  xt_gc_add_root(&proto);
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "write", (void *)xt_h2_stream_write);
  xt_node_define_method(proto, "end", (void *)xt_h2_stream_end);
  xt_node_define_method(proto, "close", (void *)xt_h2_stream_close);
  xt_node_define_method(proto, "setEncoding", (void *)xt_h2_stream_set_encoding);
  xt_node_define_method(proto, "pause", (void *)xt_h2_noop);
  xt_node_define_method(proto, "resume", (void *)xt_h2_noop);
  return proto;
}

static xt_value xt_h2_request_proto(void) {
  static xt_value proto = 0;
  xt_gc_add_root(&proto);
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  /* Node's server-side `stream` argument combines the request emitter with the
   * response API, so the request object forwards these to `__res`. */
  xt_node_define_method(proto, "respond", (void *)xt_h2_request_respond);
  xt_node_define_method(proto, "writeHead", (void *)xt_h2_request_respond);
  xt_node_define_method(proto, "write", (void *)xt_h2_request_write);
  xt_node_define_method(proto, "end", (void *)xt_h2_request_end);
  xt_node_define_method(proto, "setHeader", (void *)xt_h2_request_set_header);
  xt_node_define_method(proto, "getHeader", (void *)xt_h2_request_get_header);
  xt_node_define_method(proto, "getHeaders", (void *)xt_h2_request_get_headers);
  xt_node_define_method(proto, "flushHeaders", (void *)xt_h2_response_flush);
  xt_node_define_method(proto, "setEncoding", (void *)xt_h2_stream_set_encoding);
  xt_node_define_method(proto, "pause", (void *)xt_h2_noop);
  xt_node_define_method(proto, "resume", (void *)xt_h2_noop);
  xt_node_define_method(proto, "close", (void *)xt_h2_stream_close);
  return proto;
}

static xt_value xt_h2_response_proto(void) {
  static xt_value proto = 0;
  xt_gc_add_root(&proto);
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "writeHead", (void *)xt_h2_response_write_head);
  xt_node_define_method(proto, "setHeader", (void *)xt_h2_response_set_header);
  xt_node_define_method(proto, "getHeader", (void *)xt_h2_response_get_header);
  xt_node_define_method(proto, "getHeaders", (void *)xt_h2_response_get_headers);
  xt_node_define_method(proto, "removeHeader", (void *)xt_h2_response_remove_header);
  xt_node_define_method(proto, "hasHeader", (void *)xt_h2_response_has_header);
  xt_node_define_method(proto, "write", (void *)xt_h2_response_write);
  xt_node_define_method(proto, "end", (void *)xt_h2_response_end);
  xt_node_define_method(proto, "flushHeaders", (void *)xt_h2_response_flush);
  xt_node_define_method(proto, "close", (void *)xt_h2_response_close);
  return proto;
}

static xt_value xt_h2_session_object(xt_h2_conn *conn) {
  xt_value session = xt_object_new_with_proto(xt_h2_client_session_proto());
  xt_node_set(session, "__h2", xt_number((double)(uintptr_t)conn));
  xt_node_set(session, "__streams", xt_array_new(0, NULL));
  xt_node_set(session, "__closed", xt_bool(0));
  xt_node_set(session, "type", xt_string_from_cstr(conn->is_server ? "server" : "client"));
  xt_node_set(session, "destroyed", xt_bool(0));
  xt_node_set(session, "authority", xt_string_from_cstr(conn->authority));
  xt_node_set(session, "scheme", xt_string_from_cstr(conn->scheme));
  return session;
}

static xt_value xt_h2_new_stream(xt_h2_conn *conn, int32_t id, xt_h2_body *body) {
  xt_value stream = xt_object_new_with_proto(xt_h2_stream_proto());
  xt_node_set(stream, "__h2", xt_number((double)(uintptr_t)conn));
  xt_node_set(stream, "__id", xt_number((double)id));
  xt_node_set(stream, "__headers", xt_object_new());
  xt_node_set(stream, "__body", xt_number((double)(uintptr_t)body));
  xt_node_set(stream, "__ended", xt_bool(0));
  xt_node_set(stream, "__closed", xt_bool(0));
  xt_node_set(stream, "__encoding", xt_undefined());
  xt_node_set(stream, "id", xt_number((double)id));
  xt_h2_stream_put(conn, id, stream);
  return stream;
}
