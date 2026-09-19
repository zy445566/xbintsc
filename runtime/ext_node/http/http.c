/*
 * Node.js `http` for xbintsc.
 *
 * The server side wraps a `net` server: each accepted socket accumulates bytes
 * until a full request (headers plus any Content-Length body) is available, at
 * which point the `request` listener is invoked with a `req`/`res` pair. The
 * client side wraps a `net` socket, writes a simple HTTP/1.1 request and
 * parses the buffered response once the connection closes.
 */

#include "../node_common.h"

#include <string.h>

/* Provided by net.c (linked whenever the node extension is present). */
extern xt_value xt_net_static(xt_value name, int32_t argc, xt_value *argv);

/* Byte helpers exported by buffer.c. */
int xt_node_is_buffer(xt_value value);
unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength);

static xt_value xt_http_client_finish(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);

static xt_value xt_http_noop(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return thisValue;
}

static xt_value xt_http_server_proto(void);
static xt_value xt_http_response_proto(void);
static xt_value xt_http_request_proto(void);
static xt_value xt_http_client_proto(void);
static xt_value xt_http_client_response_proto(void);

static const char *xt_http_status_message(int code) {
  switch (code) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 418: return "I'm a Teapot";
    case 422: return "Unprocessable Entity";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return "OK";
  }
}

/* -- tiny string builder -------------------------------------------------- */

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} xt_http_buffer;

static void xt_http_buffer_init(xt_http_buffer *buffer) {
  buffer->capacity = 256;
  buffer->length = 0;
  buffer->data = (char *)malloc(buffer->capacity);
  buffer->data[0] = '\0';
}

static void xt_http_buffer_append(xt_http_buffer *buffer, const char *data, size_t length) {
  if (buffer->length + length + 1 > buffer->capacity) {
    while (buffer->length + length + 1 > buffer->capacity) buffer->capacity *= 2;
    buffer->data = (char *)realloc(buffer->data, buffer->capacity);
  }
  memcpy(buffer->data + buffer->length, data, length);
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
}

static void xt_http_buffer_free(xt_http_buffer *buffer) { free(buffer->data); }

/* Parse `name: value` header lines into an object with lower-cased keys. */
static xt_value xt_http_parse_headers(const char *headers, size_t length) {
  xt_value object = xt_object_new();
  size_t i = 0;
  while (i < length) {
    size_t lineEnd = i;
    while (lineEnd < length && headers[lineEnd] != '\n') lineEnd++;
    size_t lineLength = lineEnd - i;
    while (lineLength > 0 && (headers[i + lineLength - 1] == '\r' || headers[i + lineLength - 1] == ' ')) lineLength--;
    const char *colon = memchr(headers + i, ':', lineLength);
    if (colon) {
      size_t nameLength = (size_t)(colon - (headers + i));
      const char *value = colon + 1;
      size_t valueLength = lineLength - nameLength - 1;
      while (valueLength > 0 && *value == ' ') {
        value++;
        valueLength--;
      }
      char *name = (char *)malloc(nameLength + 1);
      for (size_t j = 0; j < nameLength; j++) {
        char c = headers[i + j];
        name[j] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
      }
      name[nameLength] = '\0';
      xt_object_set(object, xt_string_from_cstr(name), xt_string_new(value, valueLength));
      free(name);
    }
    i = lineEnd + 1;
  }
  return object;
}

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

/* -- prototypes ----------------------------------------------------------- */

static xt_value xt_http_server_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "listen", (void *)xt_http_server_listen);
  xt_node_define_method(proto, "close", (void *)xt_http_server_close);
  xt_node_define_method(proto, "address", (void *)xt_http_server_address);
  return proto;
}

static xt_value xt_http_response_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "writeHead", (void *)xt_http_response_write_head);
  xt_node_define_method(proto, "setHeader", (void *)xt_http_response_set_header);
  xt_node_define_method(proto, "getHeader", (void *)xt_http_response_get_header);
  xt_node_define_method(proto, "removeHeader", (void *)xt_http_response_remove_header);
  xt_node_define_method(proto, "getHeaders", (void *)xt_http_response_get_headers);
  xt_node_define_method(proto, "write", (void *)xt_http_response_write);
  xt_node_define_method(proto, "end", (void *)xt_http_response_end);
  return proto;
}

static xt_value xt_http_request_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "setEncoding", (void *)xt_http_noop);
  return proto;
}

static xt_value xt_http_client_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "write", (void *)xt_http_client_write);
  xt_node_define_method(proto, "end", (void *)xt_http_client_end);
  xt_node_define_method(proto, "setHeader", (void *)xt_http_client_set_header);
  xt_node_define_method(proto, "abort", (void *)xt_http_client_abort);
  return proto;
}

static xt_value xt_http_client_response_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "setEncoding", (void *)xt_http_noop);
  return proto;
}
