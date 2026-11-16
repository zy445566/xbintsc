/*
 * xbintsc runtime — the `Headers` and `Response` globals.
 *
 * `fetch` builds both as ordinary objects with own method closures; these
 * constructors reuse the same builders and chain the objects onto a shared
 * prototype, so `new Headers(...)` / `new Response(...)` work, the platform
 * methods are reachable through the prototype, and `instanceof` holds for
 * fetch's own objects too.
 */

/* -- Headers -------------------------------------------------------------- */

xt_value xt_web_headers_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (proto != XT_UNDEFINED) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xt_fetch_set_method(proto, "get", (void *)xt_headers_method_get);
  xt_fetch_set_method(proto, "has", (void *)xt_headers_method_has);
  xt_fetch_set_method(proto, "set", (void *)xt_headers_method_set);
  xt_fetch_set_method(proto, "append", (void *)xt_headers_method_append);
  xt_fetch_set_method(proto, "delete", (void *)xt_headers_method_delete);
  xt_fetch_set_method(proto, "forEach", (void *)xt_headers_method_forEach);
  xt_fetch_set_method(proto, "keys", (void *)xt_headers_method_keys);
  xt_fetch_set_method(proto, "values", (void *)xt_headers_method_values);
  xt_fetch_set_method(proto, "entries", (void *)xt_headers_method_entries);
  xt_fetch_set_method(proto, "getSetCookie", (void *)xt_headers_method_get_set_cookie);
  xt_fetch_set_method(proto, "toString", (void *)xt_headers_method_to_string);
  return proto;
}

xt_value xt_headers_ctor(int32_t argc, xt_value *argv) {
  xt_value headers = xt_headers_from_source(xt_arg(argc, argv, 0));
  xt_object_set_prototype(headers, xt_web_headers_proto());
  return headers;
}

/* -- Response ------------------------------------------------------------- */

xt_value xt_web_response_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (proto != XT_UNDEFINED) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xt_fetch_set_method(proto, "text", (void *)xt_response_method_text);
  xt_fetch_set_method(proto, "json", (void *)xt_response_method_json);
  xt_fetch_set_method(proto, "arrayBuffer", (void *)xt_response_method_array_buffer);
  xt_fetch_set_method(proto, "bytes", (void *)xt_response_method_bytes);
  xt_fetch_set_method(proto, "clone", (void *)xt_response_method_clone);
  return proto;
}

/* The body bytes of a `BodyInit`: strings keep their bytes, anything else goes
 * through the ordinary string conversion (Blob / FormData bodies are not
 * modelled yet). */
static void xt_web_body_bytes(xt_value body, const char **data, size_t *length) {
  *data = "";
  *length = 0;
  if (body == XT_UNDEFINED || body == XT_NULL) return;
  xt_value text = XT_IS_STRING(body) ? body : xt_to_string(body);
  *data = xt_string_data(text);
  *length = (size_t)xt_string_length_value(text);
}

xt_value xt_response_ctor(int32_t argc, xt_value *argv) {
  xt_value body = xt_arg(argc, argv, 0);
  xt_value init = xt_arg(argc, argv, 1);
  int status = 200;
  const char *statusText = "";
  xt_value headers = XT_UNDEFINED;
  if (XT_IS_OBJECT(init)) {
    headers = xt_object_get_cstr(init, "headers");
    xt_value statusValue = xt_object_get_cstr(init, "status");
    if (XT_IS_NUMBER(statusValue)) status = (int)xt_to_number(statusValue);
    xt_value textValue = xt_object_get_cstr(init, "statusText");
    if (XT_IS_STRING(textValue)) statusText = xt_string_data(textValue);
  }
  const char *data = "";
  size_t length = 0;
  xt_web_body_bytes(body, &data, &length);
  return xt_response_new(xt_headers_from_source(headers), status, statusText, "", data, length);
}
