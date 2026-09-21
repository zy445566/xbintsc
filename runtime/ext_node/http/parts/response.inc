/* -- response ------------------------------------------------------------- */

static xt_value xt_http_response_write_head(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc < 1) return thisValue;
  xt_node_set(thisValue, "__statusCode", xt_number(xt_to_number(argv[0])));
  if (argc > 1 && XT_IS_STRING(argv[1])) xt_node_set(thisValue, "__statusMessage", argv[1]);
  for (int32_t i = 1; i < argc; i++) {
    if (XT_IS_OBJECT(argv[i])) {
      xt_value headers = xt_object_get_cstr(thisValue, "__headers");
      xt_value keys = xt_object_keys(argv[i]);
      int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
      for (int32_t k = 0; k < count; k++) {
        xt_value key = xt_array_get(keys, xt_number((double)k));
        xt_object_set(headers, key, xt_object_get(argv[i], key));
      }
    }
  }
  return thisValue;
}

static xt_value xt_http_response_set_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value headers = xt_object_get_cstr(thisValue, "__headers");
  xt_object_set(headers, xt_arg(argc, argv, 0), xt_arg(argc, argv, 1));
  return thisValue;
}

static xt_value xt_http_response_get_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value headers = xt_object_get_cstr(thisValue, "__headers");
  return xt_object_get(headers, xt_arg(argc, argv, 0));
}

static xt_value xt_http_response_remove_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value headers = xt_object_get_cstr(thisValue, "__headers");
  xt_value key = xt_arg(argc, argv, 0);
  xt_value kept = xt_object_new();
  xt_value keys = xt_object_keys(headers);
  int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
  for (int32_t i = 0; i < count; i++) {
    xt_value item = xt_array_get(keys, xt_number((double)i));
    if (!xt_truthy(xt_eq(item, key))) xt_object_set(kept, item, xt_object_get(headers, item));
  }
  xt_node_set(thisValue, "__headers", kept);
  return thisValue;
}

static xt_value xt_http_response_get_headers(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get_cstr(thisValue, "__headers");
}

static xt_value xt_http_response_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value chunks = xt_object_get_cstr(thisValue, "__chunks");
  xt_array_push(chunks, xt_arg(argc, argv, 0));
  return xt_bool(1);
}

static xt_value xt_http_response_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc > 0 && !xt_truthy(xt_is_nullish(argv[0]))) {
    xt_value chunks = xt_object_get_cstr(thisValue, "__chunks");
    xt_array_push(chunks, argv[0]);
  }
  if (xt_truthy(xt_object_get_cstr(thisValue, "__ended"))) return thisValue;
  xt_node_set(thisValue, "__ended", xt_bool(1));

  /* Serialise the body. */
  xt_http_buffer body;
  xt_http_buffer_init(&body);
  xt_value chunks = xt_object_get_cstr(thisValue, "__chunks");
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

  int status = (int)xt_to_number(xt_object_get_cstr(thisValue, "__statusCode"));
  xt_value statusMessageValue = xt_object_get_cstr(thisValue, "__statusMessage");
  const char *statusMessage =
      XT_IS_STRING(statusMessageValue) ? xt_string_data(statusMessageValue) : xt_http_status_message(status);

  xt_http_buffer head;
  xt_http_buffer_init(&head);
  xt_http_buffer_append(&head, "HTTP/1.1 ", 9);
  char code[8];
  int codeLength = snprintf(code, sizeof(code), "%d", status);
  xt_http_buffer_append(&head, code, (size_t)codeLength);
  xt_http_buffer_append(&head, " ", 1);
  xt_http_buffer_append(&head, statusMessage, strlen(statusMessage));
  xt_http_buffer_append(&head, "\r\n", 2);

  xt_value headers = xt_object_get_cstr(thisValue, "__headers");
  xt_value keys = xt_object_keys(headers);
  int32_t keyCount = (int32_t)xt_to_number(xt_array_length(keys));
  int hasContentLength = 0;
  for (int32_t i = 0; i < keyCount; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    const char *name = xt_string_data(xt_to_string(key));
    if (!name || strcmp(name, "__proto__") == 0) continue;
    if (strcmp(name, "content-length") == 0) hasContentLength = 1;
    xt_value value = xt_object_get(headers, key);
    xt_http_buffer_append(&head, name, strlen(name));
    xt_http_buffer_append(&head, ": ", 2);
    const char *text = xt_string_data(xt_to_string(value));
    if (text) xt_http_buffer_append(&head, text, strlen(text));
    xt_http_buffer_append(&head, "\r\n", 2);
  }
  if (!hasContentLength) {
    char length[32];
    int lengthLength = snprintf(length, sizeof(length), "Content-Length: %zu\r\n", body.length);
    xt_http_buffer_append(&head, length, (size_t)lengthLength);
  }
  xt_http_buffer_append(&head, "Connection: close\r\n\r\n", 21);

  xt_value socket = xt_object_get_cstr(thisValue, "__socket");
  if (XT_IS_OBJECT(socket)) {
    xt_value writeFn = xt_object_get_cstr(socket, "write");
    if (XT_IS_FUNCTION(writeFn)) {
      xt_value chunk = xt_string_new(head.data, head.length);
      xt_call_with_this(writeFn, socket, 1, &chunk);
      if (body.length > 0) {
        xt_value bodyValue = xt_string_new(body.data, body.length);
        xt_call_with_this(writeFn, socket, 1, &bodyValue);
      }
      xt_value endFn = xt_object_get_cstr(socket, "end");
      if (XT_IS_FUNCTION(endFn)) xt_call_with_this(endFn, socket, 0, NULL);
    }
  }

  xt_http_buffer_free(&head);
  xt_http_buffer_free(&body);
  xt_node_emit0(thisValue, "finish");
  xt_node_emit0(thisValue, "close");
  return thisValue;
}
