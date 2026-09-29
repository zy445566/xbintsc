/* -- Headers -------------------------------------------------------------- */

/*
 * A Headers object is an ordinary JS object carrying the case-insensitive
 * header map under `__map` (lower-cased name -> value) plus a `__set-cookie`
 * array so `getSetCookie()` can preserve duplicate cookies. Methods are stored
 * as own closure properties, so `headers.get(...)` dispatches through the
 * generic `xt_call_method` path with the Headers object as `this`.
 */

static xt_value xt_headers_map(xt_value headers) {
  xt_value map = xt_object_get_cstr(headers, "__map");
  if (!XT_IS_OBJECT(map)) {
    map = xt_object_new();
    xt_object_set(headers, xt_string_from_cstr("__map"), map);
  }
  return map;
}

static xt_value xt_headers_cookies(xt_value headers) {
  xt_value cookies = xt_object_get_cstr(headers, "__set-cookie");
  if (!XT_IS_ARRAY(cookies)) {
    cookies = xt_array_new(0, NULL);
    xt_object_set(headers, xt_string_from_cstr("__set-cookie"), cookies);
  }
  return cookies;
}

static void xt_headers_append_raw(xt_value headers, const char *name, const char *value) {
  char key[256];
  xt_fetch_lowercase(name, key, sizeof(key));
  if (strcmp(key, "set-cookie") == 0) {
    xt_array_push(xt_headers_cookies(headers), xt_string_from_cstr(value));
  }
  xt_value map = xt_headers_map(headers);
  xt_value keyValue = xt_string_from_cstr(key);
  xt_value existing = xt_object_get(map, keyValue);
  if (XT_IS_STRING(existing)) {
    const char *previous = xt_string_data(existing);
    size_t total = strlen(previous) + 2 + strlen(value);
    char *joined = (char *)malloc(total + 1);
    if (!joined) abort();
    snprintf(joined, total + 1, "%s, %s", previous, value);
    xt_object_set(map, keyValue, xt_string_from_cstr(joined));
    free(joined);
  } else {
    xt_object_set(map, keyValue, xt_string_from_cstr(value));
  }
}

static void xt_headers_set_raw(xt_value headers, const char *name, const char *value) {
  char key[256];
  xt_fetch_lowercase(name, key, sizeof(key));
  if (strcmp(key, "set-cookie") == 0) {
    xt_value cookies = xt_headers_cookies(headers);
    xt_as_array(cookies)->length = 0;
    xt_array_push(cookies, xt_string_from_cstr(value));
  }
  xt_object_set(xt_headers_map(headers), xt_string_from_cstr(key), xt_string_from_cstr(value));
}

static const char *xt_headers_lookup(xt_value headers, const char *name) {
  char key[256];
  xt_fetch_lowercase(name, key, sizeof(key));
  xt_value value = xt_object_get(xt_headers_map(headers), xt_string_from_cstr(key));
  return XT_IS_STRING(value) ? xt_string_data(value) : NULL;
}

static xt_value xt_headers_get_value(xt_value headers, const char *name) {
  char key[256];
  xt_fetch_lowercase(name, key, sizeof(key));
  xt_value value = xt_object_get(xt_headers_map(headers), xt_string_from_cstr(key));
  return value == XT_UNDEFINED ? XT_NULL : value;
}

static void xt_fetch_set_method(xt_value object, const char *name, void *fn) {
  xt_object_set(object, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

/* -- Headers methods ------------------------------------------------------ */

static xt_value xt_headers_method_get(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value name = xt_to_string(xt_arg_at(argc, argv, 0));
  const char *text = xt_string_data(name);
  if (!text) return XT_NULL;
  return xt_headers_get_value(thisValue, text);
}

static xt_value xt_headers_method_has(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value name = xt_to_string(xt_arg_at(argc, argv, 0));
  const char *text = xt_string_data(name);
  if (!text) return XT_FALSE;
  return xt_headers_lookup(thisValue, text) != NULL ? XT_TRUE : XT_FALSE;
}

static xt_value xt_headers_method_set(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *name = xt_string_data(xt_to_string(xt_arg_at(argc, argv, 0)));
  const char *value = xt_string_data(xt_to_string(xt_arg_at(argc, argv, 1)));
  if (name && value) xt_headers_set_raw(thisValue, name, value);
  return XT_UNDEFINED;
}

static xt_value xt_headers_method_append(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *name = xt_string_data(xt_to_string(xt_arg_at(argc, argv, 0)));
  const char *value = xt_string_data(xt_to_string(xt_arg_at(argc, argv, 1)));
  if (name && value) xt_headers_append_raw(thisValue, name, value);
  return XT_UNDEFINED;
}

static xt_value xt_headers_method_delete(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *name = xt_string_data(xt_to_string(xt_arg_at(argc, argv, 0)));
  if (!name) return XT_UNDEFINED;
  char key[256];
  xt_fetch_lowercase(name, key, sizeof(key));
  xt_delete(xt_headers_map(thisValue), xt_string_from_cstr(key));
  return XT_UNDEFINED;
}

static xt_value xt_headers_method_forEach(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  if (!XT_IS_FUNCTION(callback)) return XT_UNDEFINED;
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  xt_value keys = xt_object_keys(xt_headers_map(thisValue));
  int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
  for (int32_t i = 0; i < count; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    xt_value args[3];
    args[0] = xt_object_get(xt_headers_map(thisValue), key);
    args[1] = key;
    args[2] = thisValue;
    xt_call_with_this(callback, thisArg, 3, args);
  }
  return XT_UNDEFINED;
}

static xt_value xt_headers_method_keys(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_keys(xt_headers_map(thisValue));
}

static xt_value xt_headers_method_values(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_values(xt_headers_map(thisValue));
}

static xt_value xt_headers_method_entries(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value keys = xt_object_keys(xt_headers_map(thisValue));
  int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
  xt_value entries = xt_array_new(0, NULL);
  for (int32_t i = 0; i < count; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    xt_value pair[2];
    pair[0] = key;
    pair[1] = xt_object_get(xt_headers_map(thisValue), key);
    xt_array_push(entries, xt_array_new(2, pair));
  }
  return entries;
}

static xt_value xt_headers_method_get_set_cookie(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value cookies = xt_object_get_cstr(thisValue, "__set-cookie");
  return XT_IS_ARRAY(cookies) ? cookies : xt_array_new(0, NULL);
}

static xt_value xt_headers_method_to_string(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value keys = xt_object_keys(xt_headers_map(thisValue));
  int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
  xt_fetch_buffer out;
  xt_fetch_buffer_init(&out);
  for (int32_t i = 0; i < count; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    xt_value value = xt_object_get(xt_headers_map(thisValue), key);
    const char *name = xt_string_data(key);
    const char *text = xt_string_data(xt_to_string(value));
    if (!name || !text) continue;
    xt_fetch_buffer_append_cstr(&out, name);
    xt_fetch_buffer_append_cstr(&out, ": ");
    xt_fetch_buffer_append_cstr(&out, text);
    xt_fetch_buffer_append_cstr(&out, "\r\n");
  }
  xt_value result = xt_string_new(out.data ? out.data : "", out.length);
  xt_fetch_buffer_free(&out);
  return result;
}

/* -- construction --------------------------------------------------------- */

static xt_value xt_headers_new(void) {
  xt_value headers = xt_object_new();
  xt_object_set(headers, xt_string_from_cstr("__map"), xt_object_new());
  xt_fetch_set_method(headers, "get", (void *)xt_headers_method_get);
  xt_fetch_set_method(headers, "has", (void *)xt_headers_method_has);
  xt_fetch_set_method(headers, "set", (void *)xt_headers_method_set);
  xt_fetch_set_method(headers, "append", (void *)xt_headers_method_append);
  xt_fetch_set_method(headers, "delete", (void *)xt_headers_method_delete);
  xt_fetch_set_method(headers, "forEach", (void *)xt_headers_method_forEach);
  xt_fetch_set_method(headers, "keys", (void *)xt_headers_method_keys);
  xt_fetch_set_method(headers, "values", (void *)xt_headers_method_values);
  xt_fetch_set_method(headers, "entries", (void *)xt_headers_method_entries);
  xt_fetch_set_method(headers, "getSetCookie", (void *)xt_headers_method_get_set_cookie);
  xt_fetch_set_method(headers, "toString", (void *)xt_headers_method_to_string);
  return headers;
}

/** Normalise a `headers` init value (plain object or Headers) into a Headers. */
static xt_value xt_headers_from_source(xt_value source) {
  xt_value headers = xt_headers_new();
  if (source == XT_UNDEFINED || source == XT_NULL) return headers;
  if (XT_IS_ARRAY(source)) {
    int32_t count = (int32_t)xt_to_number(xt_array_length(source));
    for (int32_t i = 0; i < count; i++) {
      xt_value pair = xt_array_get(source, xt_number((double)i));
      if (!XT_IS_ARRAY(pair) || xt_to_number(xt_array_length(pair)) < 2) continue;
      const char *name = xt_string_data(xt_to_string(xt_array_get(pair, xt_number(0))));
      const char *text = xt_string_data(xt_to_string(xt_array_get(pair, xt_number(1))));
      if (name && text) xt_headers_append_raw(headers, name, text);
    }
    return headers;
  }
  if (!XT_IS_OBJECT(source)) return headers;

  xt_value map = xt_object_get_cstr(source, "__map");
  if (XT_IS_OBJECT(map)) {
    xt_value keys = xt_object_keys(map);
    int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
    for (int32_t i = 0; i < count; i++) {
      xt_value key = xt_array_get(keys, xt_number((double)i));
      xt_value value = xt_object_get(map, key);
      const char *name = xt_string_data(key);
      const char *text = xt_string_data(xt_to_string(value));
      if (name && text) xt_headers_append_raw(headers, name, text);
    }
    return headers;
  }

  xt_value keys = xt_object_keys(source);
  int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
  for (int32_t i = 0; i < count; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    const char *name = xt_string_data(key);
    if (!name || strncmp(name, "__", 2) == 0) continue;
    xt_value value = xt_object_get(source, key);
    if (XT_IS_FUNCTION(value)) continue;
    if (XT_IS_ARRAY(value)) {
      xt_value items = value;
      int32_t itemCount = (int32_t)xt_to_number(xt_array_length(items));
      for (int32_t j = 0; j < itemCount; j++) {
        const char *text = xt_string_data(xt_to_string(xt_array_get(items, xt_number((double)j))));
        if (text) xt_headers_append_raw(headers, name, text);
      }
      continue;
    }
    const char *text = xt_string_data(xt_to_string(value));
    if (text) xt_headers_append_raw(headers, name, text);
  }
  return headers;
}

/* -- wire parsing --------------------------------------------------------- */

/** Parse the header block `[start, start+length)` (status line already read). */
static void xt_headers_parse_block(xt_value headers, const char *start, size_t length) {
  size_t i = 0;
  while (i < length) {
    size_t lineEnd = i;
    while (lineEnd + 1 < length && !(start[lineEnd] == '\r' && start[lineEnd + 1] == '\n')) lineEnd++;
    if (lineEnd + 1 >= length) break;
    const char *colon = (const char *)memchr(start + i, ':', lineEnd - i);
    if (colon) {
      const char *name = start + i;
      const char *nameEnd = colon;
      while (nameEnd > name && (nameEnd[-1] == ' ' || nameEnd[-1] == '\t')) nameEnd--;
      const char *value = colon + 1;
      while (value < start + lineEnd && (*value == ' ' || *value == '\t')) value++;
      const char *valueEnd = start + lineEnd;
      while (valueEnd > value && (valueEnd[-1] == ' ' || valueEnd[-1] == '\t')) valueEnd--;
      if (nameEnd > name) {
        char nameBuffer[256];
        size_t nameLength = (size_t)(nameEnd - name);
        if (nameLength >= sizeof(nameBuffer)) nameLength = sizeof(nameBuffer) - 1;
        memcpy(nameBuffer, name, nameLength);
        nameBuffer[nameLength] = '\0';
        char valueBuffer[8192];
        size_t valueLength = (size_t)(valueEnd - value);
        if (valueLength >= sizeof(valueBuffer)) valueLength = sizeof(valueBuffer) - 1;
        memcpy(valueBuffer, value, valueLength);
        valueBuffer[valueLength] = '\0';
        xt_headers_append_raw(headers, nameBuffer, valueBuffer);
      }
    }
    i = lineEnd + 2;
  }
}
