/* -- nghttp2 callbacks ---------------------------------------------------- */

/*
 * Header values stay in nghttp2-owned buffers for the duration of the callback,
 * so each one is copied into the JS header object immediately. Pseudo headers
 * are mirrored onto the request/stream (`method`, `url`, `scheme`,
 * `authority`, `status`) because that is how Node's `Http2Stream` exposes
 * them.
 */

static int xt_h2_on_begin_headers(nghttp2_session *session, const nghttp2_frame *frame,
                                  void *user_data) {
  (void)session;
  xt_h2_conn *conn = (xt_h2_conn *)user_data;
  if (!conn->is_server || frame->hd.type != NGHTTP2_HEADERS) return 0;
  if (frame->headers.cat != NGHTTP2_HCAT_REQUEST) return 0;
  int32_t id = frame->hd.stream_id;
  xt_value request = xt_object_new_with_proto(xt_h2_request_proto());
  xt_node_set(request, "__h2", xt_number((double)(uintptr_t)conn));
  xt_node_set(request, "__id", xt_number((double)id));
  xt_node_set(request, "__headers", xt_object_new());
  xt_node_set(request, "__encoding", xt_undefined());
  xt_node_set(request, "__ended", xt_bool(0));
  xt_node_set(request, "__closed", xt_bool(0));
  /* Server requests carry no outgoing body; the response owns one. */
  xt_node_set(request, "__body", xt_number(0));
  xt_node_set(request, "__res", xt_undefined());
  xt_node_set(request, "httpVersion", xt_string_from_cstr("2.0"));
  xt_node_set(request, "stream", request);
  xt_h2_stream_put(conn, id, request);
  return 0;
}

static int xt_h2_on_header(nghttp2_session *session, const nghttp2_frame *frame,
                           const uint8_t *name, size_t namelen, const uint8_t *value,
                           size_t valuelen, uint8_t flags, void *user_data) {
  (void)session;
  (void)flags;
  xt_h2_conn *conn = (xt_h2_conn *)user_data;
  xt_value stream = xt_h2_stream_at(conn, frame->hd.stream_id);
  if (!XT_IS_OBJECT(stream)) return 0;
  xt_value headers = xt_object_get_cstr(stream, "__headers");
  xt_h2_header_add(headers, name, namelen, value, valuelen);
  if (namelen > 0 && name[0] == ':') {
    xt_value text = xt_string_new((const char *)value, valuelen);
    if (namelen == 7 && memcmp(name, ":method", 7) == 0) xt_node_set(stream, "method", text);
    else if (namelen == 5 && memcmp(name, ":path", 5) == 0) xt_node_set(stream, "url", text);
    else if (namelen == 7 && memcmp(name, ":scheme", 7) == 0) xt_node_set(stream, "scheme", text);
    else if (namelen == 10 && memcmp(name, ":authority", 10) == 0) xt_node_set(stream, "authority", text);
    else if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
      xt_node_set(stream, "status", xt_number(xt_to_number(text)));
    }
  }
  return 0;
}

static int xt_h2_on_frame_recv(nghttp2_session *session, const nghttp2_frame *frame,
                               void *user_data) {
  (void)session;
  xt_h2_conn *conn = (xt_h2_conn *)user_data;
  int32_t id = frame->hd.stream_id;
  if (frame->hd.type == NGHTTP2_HEADERS) {
    xt_value stream = xt_h2_stream_at(conn, id);
    if (!XT_IS_OBJECT(stream)) return 0;
    if (conn->is_server) {
      if (frame->headers.cat != NGHTTP2_HCAT_REQUEST) return 0;
      xt_value response = xt_object_new_with_proto(xt_h2_response_proto());
      xt_node_set(response, "__h2", xt_number((double)(uintptr_t)conn));
      xt_node_set(response, "__id", xt_number((double)id));
      xt_node_set(response, "__headers", xt_object_new());
      xt_node_set(response, "__status", xt_number(200));
      xt_node_set(response, "__body", xt_number((double)(uintptr_t)xt_h2_body_new()));
      xt_node_set(response, "__submitted", xt_bool(0));
      xt_node_set(response, "__closed", xt_bool(0));
      xt_node_set(response, "stream", stream);
      xt_node_set(stream, "__res", response);
      /* Node's `stream` event is `(stream, headers)`, and the stream itself
       * carries the response API. */
      xt_value streamArgs[2] = {stream, xt_object_get_cstr(stream, "__headers")};
      if (XT_IS_OBJECT(conn->owner)) xt_node_emit(conn->owner, "stream", 2, streamArgs);
      xt_node_emit(conn->session_value, "stream", 2, streamArgs);
    } else {
      xt_value flags = xt_object_new();
      xt_node_set(flags, "endStream",
                  xt_bool((frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0));
      xt_value responseArgs[2] = {xt_object_get_cstr(stream, "__headers"), flags};
      xt_node_emit(stream, "response", 2, responseArgs);
    }
  } else if (frame->hd.type == NGHTTP2_DATA) {
    if ((frame->hd.flags & NGHTTP2_FLAG_END_STREAM) == 0) return 0;
    xt_value stream = xt_h2_stream_at(conn, id);
    if (!XT_IS_OBJECT(stream)) return 0;
    if (!xt_truthy(xt_object_get_cstr(stream, "__ended"))) {
      xt_node_set(stream, "__ended", xt_bool(1));
      xt_node_emit0(stream, "end");
    }
  }
  return 0;
}

static int xt_h2_on_data_chunk_recv(nghttp2_session *session, uint8_t flags, int32_t stream_id,
                                    const uint8_t *data, size_t len, void *user_data) {
  (void)session;
  (void)flags;
  xt_h2_conn *conn = (xt_h2_conn *)user_data;
  xt_value stream = xt_h2_stream_at(conn, stream_id);
  if (!XT_IS_OBJECT(stream)) return 0;
  xt_value encoding = xt_object_get_cstr(stream, "__encoding");
  if (XT_IS_STRING(encoding)) {
    const char *name = xt_string_data(encoding);
    xt_node_emit1(stream, "data", xt_node_encode(data, len, name));
  } else {
    xt_node_emit1(stream, "data", xt_h2_buffer(data, len));
  }
  return 0;
}

static int xt_h2_on_stream_close(nghttp2_session *session, int32_t stream_id,
                                 uint32_t error_code, void *user_data) {
  (void)session;
  (void)error_code;
  xt_h2_conn *conn = (xt_h2_conn *)user_data;
  xt_value stream = xt_h2_stream_at(conn, stream_id);
  if (!XT_IS_OBJECT(stream)) return 0;
  xt_value response = xt_object_get_cstr(stream, "__res");
  if (!xt_truthy(xt_object_get_cstr(stream, "__ended"))) {
    xt_node_set(stream, "__ended", xt_bool(1));
    xt_node_emit0(stream, "end");
  }
  xt_node_set(stream, "__closed", xt_bool(1));
  xt_node_emit0(stream, "close");
  /* Bodies are released when the session is torn down: nghttp2 may still hold
   * the data provider it was given while it reports the close. */
  if (XT_IS_OBJECT(response)) {
    xt_node_set(response, "__closed", xt_bool(1));
    xt_node_emit0(response, "close");
  }
  return 0;
}
