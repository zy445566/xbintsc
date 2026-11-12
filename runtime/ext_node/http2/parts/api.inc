/* -- API table and registration ------------------------------------------- */

xt_value xt_h2_constants(void) {
  xt_value constants = xt_object_new();
  xt_node_set(constants, "NGHTTP2_NO_ERROR", xt_number(0));
  xt_node_set(constants, "NGHTTP2_PROTOCOL_ERROR", xt_number(1));
  xt_node_set(constants, "NGHTTP2_INTERNAL_ERROR", xt_number(2));
  xt_node_set(constants, "NGHTTP2_FLOW_CONTROL_ERROR", xt_number(3));
  xt_node_set(constants, "NGHTTP2_SETTINGS_TIMEOUT", xt_number(4));
  xt_node_set(constants, "NGHTTP2_STREAM_CLOSED", xt_number(5));
  xt_node_set(constants, "NGHTTP2_FRAME_SIZE_ERROR", xt_number(6));
  xt_node_set(constants, "NGHTTP2_REFUSED_STREAM", xt_number(7));
  xt_node_set(constants, "NGHTTP2_CANCEL", xt_number(8));
  xt_node_set(constants, "NGHTTP2_COMPRESSION_ERROR", xt_number(9));
  xt_node_set(constants, "NGHTTP2_CONNECT_ERROR", xt_number(10));
  xt_node_set(constants, "NGHTTP2_ENHANCE_YOUR_CALM", xt_number(11));
  xt_node_set(constants, "NGHTTP2_INADEQUATE_SECURITY", xt_number(12));
  xt_node_set(constants, "NGHTTP2_HTTP_1_1_REQUIRED", xt_number(13));
  xt_node_set(constants, "NGHTTP2_HEADERS", xt_number(NGHTTP2_HEADERS));
  xt_node_set(constants, "NGHTTP2_DATA", xt_number(NGHTTP2_DATA));
  xt_node_set(constants, "NGHTTP2_SETTINGS", xt_number(NGHTTP2_SETTINGS));
  xt_node_set(constants, "NGHTTP2_FLAG_NONE", xt_number(NGHTTP2_FLAG_NONE));
  xt_node_set(constants, "NGHTTP2_FLAG_ACK", xt_number(NGHTTP2_FLAG_ACK));
  xt_node_set(constants, "NGHTTP2_FLAG_END_STREAM", xt_number(NGHTTP2_FLAG_END_STREAM));
  xt_node_set(constants, "NGHTTP2_FLAG_END_HEADERS", xt_number(NGHTTP2_FLAG_END_HEADERS));
  xt_node_set(constants, "NGHTTP2_DEFAULT_WEIGHT", xt_number(NGHTTP2_DEFAULT_WEIGHT));
  return constants;
}

static const XtHttp2Api kHttp2Api = {
    xt_h2_connect,
    xt_h2_create_server,
    xt_h2_create_secure_server,
    xt_h2_constants,
};

/* Called by `http2_bridge.c`, which is the feature's compiled `runtimeSources()`
 * entry. The call is what makes the linker extract this object (and the nghttp2
 * members it references) from `nghttp2.lib`. */
void xt_http2_backend_boot(void) { xt_http2_register(&kHttp2Api); }
