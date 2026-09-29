/* -- Response ------------------------------------------------------------- */

/*
 * `fetch` resolves with a plain object shaped like a `Response`: the status
 * fields are own properties, `headers` is a Headers object, and the body is
 * kept verbatim under `__body`. The body consumers (`text` / `json` /
 * `arrayBuffer` / `bytes`) return promises, matching the platform API even
 * though the value is already available.
 */

static void *xt_response_method_impl(xt_value name);
static xt_value xt_method_value_for_response(xt_value name);

static xt_value xt_response_body(xt_value response) {
  xt_value body = xt_object_get_cstr(response, "__body");
  return XT_IS_STRING(body) ? body : xt_string_from_cstr("");
}

static xt_value xt_response_method_text(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_promise_resolve(xt_response_body(thisValue));
}

static xt_value xt_response_method_json(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value body = xt_response_body(thisValue);
  return xt_promise_resolve(xt_json_parse(1, &body));
}

static xt_value xt_response_method_array_buffer(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value body = xt_response_body(thisValue);
  const char *data = xt_string_data(body);
  int32_t length = xt_string_length_value(body);
  if (!data || length < 0) length = 0;
  xt_value bytes = xt_array_new(0, NULL);
  for (int32_t i = 0; i < length; i++) {
    xt_array_push(bytes, xt_number((double)(unsigned char)data[i]));
  }
  return xt_promise_resolve(bytes);
}

static xt_value xt_response_method_clone(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value clone = xt_object_new();
  xt_value keys = xt_object_keys(thisValue);
  int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
  for (int32_t i = 0; i < count; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    xt_value value = xt_object_get(thisValue, key);
    if (XT_IS_FUNCTION(value)) {
      /* Re-use the method implementation with the new receiver. */
      xt_value name = key;
      xt_object_set(clone, name, xt_method_value_for_response(name));
    } else {
      xt_object_set(clone, key, value);
    }
  }
  return clone;
}

/* Map a Response method name back to its implementation so `clone()` can copy
 * the methods without duplicating the dispatch table. */
static void *xt_response_method_impl(xt_value name) {
  const char *method = xt_string_data(name);
  if (!method) return NULL;
  if (strcmp(method, "text") == 0) return (void *)xt_response_method_text;
  if (strcmp(method, "json") == 0) return (void *)xt_response_method_json;
  if (strcmp(method, "arrayBuffer") == 0) return (void *)xt_response_method_array_buffer;
  if (strcmp(method, "bytes") == 0) return (void *)xt_response_method_array_buffer;
  if (strcmp(method, "clone") == 0) return (void *)xt_response_method_clone;
  return NULL;
}

static xt_value xt_method_value_for_response(xt_value name) {
  void *impl = xt_response_method_impl(name);
  return impl ? xt_closure_new(impl, 0, NULL) : XT_UNDEFINED;
}

static xt_value xt_response_new(xt_value headers, int status, const char *statusText, const char *url,
                                const char *body, size_t bodyLength) {
  xt_value response = xt_object_new();
  xt_object_set(response, xt_string_from_cstr("ok"),
                (status >= 200 && status < 300) ? XT_TRUE : XT_FALSE);
  xt_object_set(response, xt_string_from_cstr("status"), xt_number((double)status));
  xt_object_set(response, xt_string_from_cstr("statusText"), xt_string_from_cstr(statusText ? statusText : ""));
  xt_object_set(response, xt_string_from_cstr("url"), xt_string_from_cstr(url ? url : ""));
  xt_object_set(response, xt_string_from_cstr("redirected"), XT_FALSE);
  xt_object_set(response, xt_string_from_cstr("type"), xt_string_from_cstr("basic"));
  xt_object_set(response, xt_string_from_cstr("bodyUsed"), XT_FALSE);
  xt_object_set(response, xt_string_from_cstr("headers"), headers);
  xt_object_set(response, xt_string_from_cstr("__body"), xt_string_new(body ? body : "", bodyLength));
  xt_fetch_set_method(response, "text", (void *)xt_response_method_text);
  xt_fetch_set_method(response, "json", (void *)xt_response_method_json);
  xt_fetch_set_method(response, "arrayBuffer", (void *)xt_response_method_array_buffer);
  xt_fetch_set_method(response, "bytes", (void *)xt_response_method_array_buffer);
  xt_fetch_set_method(response, "clone", (void *)xt_response_method_clone);
  return response;
}
