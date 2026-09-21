/* -- client --------------------------------------------------------------- */

static void xt_http_parse_url(xt_value urlValue, const char **host, int *port, const char **path) {
  *host = "127.0.0.1";
  *port = 80;
  *path = "/";
  const char *url = xt_string_data(xt_to_string(urlValue));
  if (!url) return;
  static char hostBuffer[256];
  static char pathBuffer[1024];
  const char *cursor = url;
  if (strncmp(cursor, "http://", 7) == 0) cursor += 7;
  else if (strncmp(cursor, "https://", 8) == 0) cursor += 8;
  const char *slash = strchr(cursor, '/');
  const char *hostEnd = slash ? slash : cursor + strlen(cursor);
  const char *colon = memchr(cursor, ':', (size_t)(hostEnd - cursor));
  if (colon) {
    size_t hostLength = (size_t)(colon - cursor);
    if (hostLength >= sizeof(hostBuffer)) hostLength = sizeof(hostBuffer) - 1;
    memcpy(hostBuffer, cursor, hostLength);
    hostBuffer[hostLength] = '\0';
    *host = hostBuffer;
    *port = atoi(colon + 1);
  } else {
    size_t hostLength = (size_t)(hostEnd - cursor);
    if (hostLength >= sizeof(hostBuffer)) hostLength = sizeof(hostBuffer) - 1;
    memcpy(hostBuffer, cursor, hostLength);
    hostBuffer[hostLength] = '\0';
    *host = hostBuffer;
  }
  if (slash) {
    size_t pathLength = strlen(slash);
    if (pathLength >= sizeof(pathBuffer)) pathLength = sizeof(pathBuffer) - 1;
    memcpy(pathBuffer, slash, pathLength);
    pathBuffer[pathLength] = '\0';
    *path = pathBuffer;
  }
}

static xt_value xt_http_client_data(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
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
  return xt_undefined();
}

static xt_value xt_http_client_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_array_push(xt_object_get_cstr(thisValue, "__chunks"), xt_arg(argc, argv, 0));
  return xt_bool(1);
}

static void xt_http_client_send(xt_value client) {
  if (xt_truthy(xt_object_get_cstr(client, "__sent"))) return;
  xt_node_set(client, "__sent", xt_bool(1));

  const char *host = "127.0.0.1";
  int port = 80;
  const char *path = "/";
  xt_value urlValue = xt_object_get_cstr(client, "__url");
  if (XT_IS_STRING(urlValue)) xt_http_parse_url(urlValue, &host, &port, &path);

  const char *method = "GET";
  xt_value methodValue = xt_object_get_cstr(client, "method");
  if (XT_IS_STRING(methodValue)) method = xt_string_data(methodValue);

  xt_http_buffer body;
  xt_http_buffer_init(&body);
  xt_value chunks = xt_object_get_cstr(client, "__chunks");
  int32_t count = (int32_t)xt_to_number(xt_array_length(chunks));
  for (int32_t i = 0; i < count; i++) {
    xt_value chunk = xt_array_get(chunks, xt_number((double)i));
    if (xt_node_is_buffer(chunk)) {
      size_t length = 0;
      unsigned char *bytes = xt_node_buffer_bytes(chunk, &length);
      xt_http_buffer_append(&body, (const char *)bytes, length);
      free(bytes);
    } else {
      const char *text = xt_string_data(xt_to_string(chunk));
      if (text) xt_http_buffer_append(&body, text, strlen(text));
    }
  }

  xt_http_buffer head;
  xt_http_buffer_init(&head);
  xt_http_buffer_append(&head, method, strlen(method));
  xt_http_buffer_append(&head, " ", 1);
  xt_http_buffer_append(&head, path, strlen(path));
  xt_http_buffer_append(&head, " HTTP/1.1\r\nHost: ", 17);
  xt_http_buffer_append(&head, host, strlen(host));
  char portBuffer[16];
  int portLength = snprintf(portBuffer, sizeof(portBuffer), ":%d", port);
  xt_http_buffer_append(&head, portBuffer, (size_t)portLength);
  xt_http_buffer_append(&head, "\r\n", 2);

  xt_value headers = xt_object_get_cstr(client, "__headers");
  xt_value keys = xt_object_keys(headers);
  int32_t keyCount = (int32_t)xt_to_number(xt_array_length(keys));
  for (int32_t i = 0; i < keyCount; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    const char *name = xt_string_data(xt_to_string(key));
    if (!name) continue;
    xt_http_buffer_append(&head, name, strlen(name));
    xt_http_buffer_append(&head, ": ", 2);
    const char *text = xt_string_data(xt_to_string(xt_object_get(headers, key)));
    if (text) xt_http_buffer_append(&head, text, strlen(text));
    xt_http_buffer_append(&head, "\r\n", 2);
  }
  char lengthBuffer[128];
  int lengthLength = snprintf(lengthBuffer, sizeof(lengthBuffer), "Content-Length: %zu\r\nConnection: close\r\n\r\n", body.length);
  xt_http_buffer_append(&head, lengthBuffer, (size_t)lengthLength);
  if (body.length > 0) xt_http_buffer_append(&head, body.data, body.length);

  xt_value connectArgs[2];
  connectArgs[0] = xt_number((double)port);
  connectArgs[1] = xt_string_from_cstr(host);
  xt_value socket = xt_net_static(xt_string_from_cstr("connect"), 2, connectArgs);
  xt_node_set(client, "__socket", socket);
  xt_node_set(socket, "__httpBuffer", xt_string_from_cstr(""));

  xt_value listener = xt_closure_new((void *)xt_http_client_data, 1, &client);
  xt_array_push(xt_node_listeners(socket, "data", 1), listener);
  xt_array_push(xt_node_listeners(socket, "end", 1), xt_closure_new((void *)xt_http_client_finish, 1, &client));

  xt_value writeFn = xt_object_get_cstr(socket, "write");
  if (XT_IS_FUNCTION(writeFn)) {
    xt_value payload = xt_string_new(head.data, head.length);
    xt_call_with_this(writeFn, socket, 1, &payload);
  }

  xt_http_buffer_free(&head);
  xt_http_buffer_free(&body);
}

static xt_value xt_http_client_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc > 0 && !xt_truthy(xt_is_nullish(argv[0]))) {
    xt_array_push(xt_object_get_cstr(thisValue, "__chunks"), argv[0]);
  }
  xt_http_client_send(thisValue);
  return thisValue;
}

static xt_value xt_http_client_set_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_object_set(xt_object_get_cstr(thisValue, "__headers"), xt_arg(argc, argv, 0), xt_arg(argc, argv, 1));
  return thisValue;
}

static xt_value xt_http_client_abort(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value socket = xt_object_get_cstr(thisValue, "__socket");
  if (XT_IS_OBJECT(socket)) {
    xt_value destroyFn = xt_object_get_cstr(socket, "destroy");
    if (XT_IS_FUNCTION(destroyFn)) xt_call_with_this(destroyFn, socket, 0, NULL);
  }
  return thisValue;
}

/* Called when the response socket closes: parse the buffered response. */
static xt_value xt_http_client_finish(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_value client = xt_closure_env(env, 0);
  xt_value socket = xt_object_get_cstr(client, "__socket");
  xt_value bufferValue = xt_object_get_cstr(socket, "__httpBuffer");
  const char *buffer = XT_IS_STRING(bufferValue) ? xt_string_data(bufferValue) : "";
  size_t length = buffer ? strlen(buffer) : 0;

  xt_value res = xt_object_new_with_proto(xt_http_client_response_proto());
  int status = 0;
  size_t headerLength = 0;
  const char *headerEnd = NULL;
  for (size_t i = 0; i + 3 < length; i++) {
    if (buffer[i] == '\r' && buffer[i + 1] == '\n' && buffer[i + 2] == '\r' && buffer[i + 3] == '\n') {
      headerEnd = buffer + i;
      headerLength = i + 4;
      break;
    }
  }

  xt_value headers = xt_object_new();
  const char *body = "";
  size_t bodyLength = 0;
  if (headerEnd) {
    const char *lineEnd = strstr(buffer, "\r\n");
    if (lineEnd) {
      const char *firstSpace = memchr(buffer, ' ', (size_t)(lineEnd - buffer));
      if (firstSpace) status = atoi(firstSpace + 1);
    }
    headers = xt_http_parse_headers(buffer + (lineEnd ? (lineEnd - buffer) + 2 : 0),
                                    headerLength - (size_t)((lineEnd ? (lineEnd - buffer) : 0) + 2) - 2);
    body = buffer + headerLength;
    bodyLength = length - headerLength;
  } else {
    body = buffer;
    bodyLength = length;
  }

  xt_node_set(res, "statusCode", xt_number((double)status));
  xt_node_set(res, "statusMessage", xt_string_from_cstr(xt_http_status_message(status)));
  xt_node_set(res, "httpVersion", xt_string_from_cstr("1.1"));
  xt_node_set(res, "headers", headers);
  xt_node_set(res, "rawHeaders", xt_array_new(0, NULL));
  xt_node_set(res, "complete", xt_bool(1));
  xt_value bodyValue = xt_string_new(body, bodyLength);
  xt_node_set(res, "__body", bodyValue);

  xt_node_emit1(client, "response", res);
  if (bodyLength > 0) xt_node_emit1(res, "data", bodyValue);
  xt_node_emit0(res, "end");
  xt_node_emit0(res, "close");
  return xt_undefined();
}

static xt_value xt_http_create_client_request(int32_t argc, xt_value *argv) {
  xt_value client = xt_object_new_with_proto(xt_http_client_proto());
  xt_node_set(client, "__headers", xt_object_new());
  xt_node_set(client, "__chunks", xt_array_new(0, NULL));

  const char *method = "GET";
  const char *host = "127.0.0.1";
  int port = 80;
  const char *path = "/";

  int index = 0;
  if (argc > 0 && XT_IS_STRING(argv[0])) {
    xt_value urlValue = argv[0];
    xt_node_set(client, "__url", urlValue);
    xt_http_parse_url(urlValue, &host, &port, &path);
    index = 1;
  } else if (argc > 0 && XT_IS_OBJECT(argv[0])) {
    xt_value options = argv[0];
    xt_value hostValue = xt_object_get_cstr(options, "hostname");
    if (!XT_IS_STRING(hostValue)) hostValue = xt_object_get_cstr(options, "host");
    if (XT_IS_STRING(hostValue)) host = xt_string_data(hostValue);
    xt_value portValue = xt_object_get_cstr(options, "port");
    if (!xt_truthy(xt_is_nullish(portValue))) port = (int)xt_to_number(portValue);
    xt_value pathValue = xt_object_get_cstr(options, "path");
    if (XT_IS_STRING(pathValue)) path = xt_string_data(pathValue);
    xt_value methodValue = xt_object_get_cstr(options, "method");
    if (XT_IS_STRING(methodValue)) method = xt_string_data(methodValue);
    xt_value headers = xt_object_get_cstr(options, "headers");
    if (XT_IS_OBJECT(headers)) xt_node_set(client, "__headers", headers);
    index = 1;
  }

  for (int32_t i = index; i < argc; i++) {
    if (XT_IS_FUNCTION(argv[i])) xt_array_push(xt_node_listeners(client, "response", 1), argv[i]);
  }

  /* Give the parser a synthetic URL when only an options object was supplied. */
  if (xt_truthy(xt_is_nullish(xt_object_get_cstr(client, "__url")))) {
    char url[512];
    snprintf(url, sizeof(url), "http://%s:%d%s", host, port, path);
    xt_node_set(client, "__url", xt_string_from_cstr(url));
  }
  xt_node_set(client, "method", xt_string_from_cstr(method));
  xt_node_set(client, "path", xt_string_from_cstr(path));
  return client;
}

xt_value xt_http_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return xt_undefined();
  if (strcmp(fn, "createServer") == 0) return xt_http_create_server(argc, argv);
  if (strcmp(fn, "request") == 0) return xt_http_create_client_request(argc, argv);
  if (strcmp(fn, "get") == 0) {
    xt_value client = xt_http_create_client_request(argc, argv);
    xt_value endFn = xt_object_get_cstr(client, "end");
    if (XT_IS_FUNCTION(endFn)) xt_call_with_this(endFn, client, 0, NULL);
    return client;
  }
  return xt_undefined();
}
