/* -- server request handling ---------------------------------------------- */

static void xt_http_serve(xt_value socket, xt_value server) {
  xt_value bufferValue = xt_object_get_cstr(socket, "__httpBuffer");
  const char *buffer = XT_IS_STRING(bufferValue) ? xt_string_data(bufferValue) : "";
  size_t length = buffer ? strlen(buffer) : 0;
  const char *headerEnd = NULL;
  for (size_t i = 0; i + 3 < length; i++) {
    if (buffer[i] == '\r' && buffer[i + 1] == '\n' && buffer[i + 2] == '\r' && buffer[i + 3] == '\n') {
      headerEnd = buffer + i;
      break;
    }
  }
  if (!headerEnd) return;

  size_t headerLength = (size_t)(headerEnd - buffer) + 4;
  const char *lineEnd = strstr(buffer, "\r\n");
  if (!lineEnd) return;
  const char *methodEnd = strchr(buffer, ' ');
  const char *urlEnd = methodEnd ? strchr(methodEnd + 1, ' ') : NULL;
  if (!methodEnd || !urlEnd || urlEnd > lineEnd) return;

  xt_value req = xt_object_new_with_proto(xt_http_request_proto());
  xt_node_set(req, "method", xt_string_new(buffer, (size_t)(methodEnd - buffer)));
  xt_node_set(req, "url", xt_string_new(methodEnd + 1, (size_t)(urlEnd - methodEnd - 1)));
  const char *version = urlEnd + 1;
  size_t versionLength = (size_t)(lineEnd - version);
  xt_node_set(req, "httpVersion", xt_string_new(version, versionLength));

  xt_value headers = xt_http_parse_headers(buffer + (lineEnd - buffer) + 2, headerLength - (size_t)(lineEnd - buffer) - 4);
  xt_node_set(req, "headers", headers);

  int contentLength = 0;
  xt_value contentLengthValue = xt_object_get_cstr(headers, "content-length");
  if (!xt_truthy(xt_is_nullish(contentLengthValue))) contentLength = (int)xt_to_number(contentLengthValue);
  if (length < headerLength + (size_t)contentLength) return;

  xt_value body = xt_string_new(buffer + headerLength, (size_t)contentLength);
  xt_node_set(req, "__body", body);

  xt_value res = xt_object_new_with_proto(xt_http_response_proto());
  xt_node_set(res, "__socket", socket);
  xt_node_set(res, "__headers", xt_object_new());
  xt_node_set(res, "__chunks", xt_array_new(0, NULL));
  xt_node_set(res, "__statusCode", xt_number(200));
  xt_node_set(res, "__statusMessage", xt_string_from_cstr("OK"));

  /* Clear the consume buffer before dispatching. */
  xt_node_set(socket, "__httpBuffer", xt_string_from_cstr(""));

  xt_node_set(req, "__ended", xt_bool(0));
  if (contentLength > 0) xt_node_emit1(req, "data", body);
  xt_node_emit0(req, "end");

  xt_node_emit(server, "request", 2, (xt_value[]){req, res});

  if (!xt_truthy(xt_object_get_cstr(res, "__ended"))) {
    xt_value endFn = xt_object_get_cstr(res, "end");
    if (XT_IS_FUNCTION(endFn)) xt_call_with_this(endFn, res, 0, NULL);
  }
}

static xt_value xt_http_server_data(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  xt_value server = xt_closure_env(env, 0);
  xt_value chunk = xt_arg(argc, argv, 0);
  xt_value existing = xt_object_get_cstr(thisValue, "__httpBuffer");
  const char *previous = XT_IS_STRING(existing) ? xt_string_data(existing) : "";
  const char *addition = xt_string_data(xt_to_string(chunk));
  if (!addition) return xt_undefined();
  size_t previousLength = previous ? strlen(previous) : 0;
  size_t additionLength = strlen(addition);
  char *combined = (char *)malloc(previousLength + additionLength + 1);
  if (previousLength) memcpy(combined, previous, previousLength);
  memcpy(combined + previousLength, addition, additionLength);
  combined[previousLength + additionLength] = '\0';
  xt_value combinedValue = xt_string_new(combined, previousLength + additionLength);
  free(combined);
  xt_object_set(thisValue, xt_string_from_cstr("__httpBuffer"), combinedValue);
  xt_http_serve(thisValue, server);
  return xt_undefined();
}

static xt_value xt_http_server_connection(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value server = xt_closure_env(env, 0);
  xt_value socket = xt_arg(argc, argv, 0);
  xt_node_set(socket, "__httpBuffer", xt_string_from_cstr(""));
  xt_value listener = xt_closure_new((void *)xt_http_server_data, 1, &server);
  xt_array_push(xt_node_listeners(socket, "data", 1), listener);
  return xt_undefined();
}

/* -- server construction / delegation to net ------------------------------ */

static xt_value xt_http_server_listen(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value netServer = xt_object_get_cstr(thisValue, "__net");
  xt_value listenFn = xt_object_get_cstr(netServer, "listen");
  if (XT_IS_FUNCTION(listenFn)) xt_call_with_this(listenFn, netServer, argc, argv);
  return thisValue;
}

static xt_value xt_http_server_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value netServer = xt_object_get_cstr(thisValue, "__net");
  xt_value closeFn = xt_object_get_cstr(netServer, "close");
  if (XT_IS_FUNCTION(closeFn)) xt_call_with_this(closeFn, netServer, argc, argv);
  return thisValue;
}

static xt_value xt_http_server_address(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value netServer = xt_object_get_cstr(thisValue, "__net");
  xt_value addressFn = xt_object_get_cstr(netServer, "address");
  if (XT_IS_FUNCTION(addressFn)) return xt_call_with_this(addressFn, netServer, argc, argv);
  return xt_null();
}

static xt_value xt_http_create_server(int32_t argc, xt_value *argv) {
  xt_value server = xt_object_new_with_proto(xt_http_server_proto());
  xt_value netArgs[1];
  netArgs[0] = xt_string_from_cstr("createServer");
  xt_value netServer = xt_net_static(netArgs[0], 0, NULL);
  xt_node_set(server, "__net", netServer);

  xt_value listener = xt_closure_new((void *)xt_http_server_connection, 1, &server);
  xt_array_push(xt_node_listeners(netServer, "connection", 1), listener);

  for (int32_t i = 0; i < argc; i++) {
    if (XT_IS_FUNCTION(argv[i])) xt_array_push(xt_node_listeners(server, "request", 1), argv[i]);
  }
  return server;
}
