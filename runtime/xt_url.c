/*
 * xbintsc runtime — URLSearchParams.
 *
 * The query string is stored as an array of [key, value] pairs under an
 * internal `__entries` property. Methods are closures on the instance, so the
 * same object model as the rest of the runtime applies. Values are stored
 * decoded and encoded with application/x-www-form-urlencoded rules when
 * serialized (`+` for spaces, percent escapes for everything else).
 */

#include "rt_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int xt_url_unreserved(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '*' || c == '-' || c == '.' || c == '_';
}

static xt_value xt_url_encode(xt_value value) {
  xt_value text = xt_to_string(value);
  xt_string *string = xt_as_string(text);
  const char *bytes = xt_string_bytes(string);
  size_t capacity = string->length * 3 + 1;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  size_t written = 0;
  for (uint32_t i = 0; i < string->length; i++) {
    unsigned char c = (unsigned char)bytes[i];
    if (c == ' ') {
      out[written++] = '+';
    } else if (xt_url_unreserved(c)) {
      out[written++] = (char)c;
    } else {
      static const char hex[] = "0123456789ABCDEF";
      out[written++] = '%';
      out[written++] = hex[c >> 4];
      out[written++] = hex[c & 0x0f];
    }
  }
  xt_value result = xt_string_new(out, written);
  free(out);
  return result;
}

static int xt_url_hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static xt_value xt_url_decode(const char *data, size_t length) {
  char *out = (char *)malloc(length == 0 ? 1 : length);
  if (!out) abort();
  size_t written = 0;
  for (size_t i = 0; i < length; i++) {
    char c = data[i];
    if (c == '+') {
      out[written++] = ' ';
    } else if (c == '%' && i + 2 < length) {
      int high = xt_url_hex(data[i + 1]);
      int low = xt_url_hex(data[i + 2]);
      if (high >= 0 && low >= 0) {
        out[written++] = (char)((high << 4) | low);
        i += 2;
      } else {
        out[written++] = c;
      }
    } else {
      out[written++] = c;
    }
  }
  xt_value result = xt_string_new(out, written);
  free(out);
  return result;
}

static xt_value xt_url_entries(xt_value params) {
  xt_value entries = xt_get(params, xt_string_from_cstr("__entries"));
  if (!XT_IS_ARRAY(entries)) {
    entries = xt_array_new(0, NULL);
    xt_object_set(params, xt_string_from_cstr("__entries"), entries);
  }
  return entries;
}

static void xt_url_set_size(xt_value params) {
  xt_array *entries = xt_as_array(xt_url_entries(params));
  xt_object_set(params, xt_string_from_cstr("size"), xt_number((double)entries->length));
}

static void xt_url_append_pair(xt_value entries, xt_value key, xt_value value) {
  xt_value pair = xt_array_new(0, NULL);
  xt_array_push(pair, key);
  xt_array_push(pair, value);
  xt_array_push(entries, pair);
}

static xt_value xt_url_string_at(xt_value text, size_t start, size_t length) {
  xt_string *string = xt_as_string(text);
  const char *bytes = xt_string_bytes(string);
  return xt_string_new(bytes + start, length);
}

/* Split a query string on `&`, then each item on the first `=`; decode both
 * halves and append them. A leading `?` is ignored. */
static void xt_url_parse_into(xt_value params, xt_value input) {
  xt_value text = xt_to_string(input);
  xt_string *string = xt_as_string(text);
  const char *bytes = xt_string_bytes(string);
  size_t length = string->length;
  size_t start = (length > 0 && bytes[0] == '?') ? 1 : 0;
  xt_value entries = xt_url_entries(params);
  while (start <= length) {
    size_t end = start;
    while (end < length && bytes[end] != '&') end++;
    if (end > start) {
      size_t equals = start;
      while (equals < end && bytes[equals] != '=') equals++;
      xt_value key = xt_url_decode(bytes + start, equals - start);
      xt_value value = equals < end ? xt_url_decode(bytes + equals + 1, end - equals - 1)
                                    : xt_string_from_cstr("");
      xt_url_append_pair(entries, key, value);
    }
    if (end >= length) break;
    start = end + 1;
  }
  xt_url_set_size(params);
}

static void xt_url_ctor_from_value(xt_value params, xt_value source) {
  if (source == XT_UNDEFINED) return;
  if (XT_IS_STRING(source)) {
    xt_url_parse_into(params, source);
    return;
  }
  if (XT_IS_ARRAY(source)) {
    xt_value entries = xt_url_entries(params);
    xt_array *pairs = xt_as_array(source);
    for (uint32_t i = 0; i < pairs->length; i++) {
      xt_array *pair = xt_as_array(pairs->items[i]);
      if (pair && pair->length >= 1) {
        xt_value value = pair->length >= 2 ? pair->items[1] : xt_string_from_cstr("");
        xt_url_append_pair(entries, xt_to_string(pair->items[0]), xt_to_string(value));
      }
    }
    xt_url_set_size(params);
    return;
  }
  if (XT_IS_OBJECT(source)) {
    xt_value existing = xt_get(source, xt_string_from_cstr("__entries"));
    if (XT_IS_ARRAY(existing)) {
      xt_value entries = xt_url_entries(params);
      xt_array *pairs = xt_as_array(existing);
      for (uint32_t i = 0; i < pairs->length; i++) {
        xt_array *pair = xt_as_array(pairs->items[i]);
        if (pair && pair->length >= 2) xt_url_append_pair(entries, pair->items[0], pair->items[1]);
      }
      xt_url_set_size(params);
      return;
    }
    xt_value keys = xt_object_keys(source);
    xt_array *list = xt_as_array(keys);
    if (!list) return;
    xt_value entries = xt_url_entries(params);
    for (uint32_t i = 0; i < list->length; i++) {
      xt_value key = list->items[i];
      xt_url_append_pair(entries, xt_to_string(key), xt_to_string(xt_object_get(source, key)));
    }
    xt_url_set_size(params);
  }
}

/* -- methods -------------------------------------------------------------- */

static xt_value xt_url_method_append(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value entries = xt_url_entries(thisValue);
  xt_url_append_pair(entries, xt_to_string(xt_arg_at(argc, argv, 0)),
                     xt_to_string(xt_arg_at(argc, argv, 1)));
  xt_url_set_size(thisValue);
  return XT_UNDEFINED;
}

static int xt_url_key_equals(xt_value pair, xt_value key) {
  xt_array *items = xt_as_array(pair);
  return items && items->length >= 2 && xt_value_equals(items->items[0], key);
}

static xt_value xt_url_method_get(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value key = xt_to_string(xt_arg_at(argc, argv, 0));
  xt_array *entries = xt_as_array(xt_url_entries(thisValue));
  for (uint32_t i = 0; i < entries->length; i++) {
    if (xt_url_key_equals(entries->items[i], key)) return ((xt_array *)XT_GET_PTR(entries->items[i]))->items[1];
  }
  return XT_NULL;
}

static xt_value xt_url_method_get_all(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value key = xt_to_string(xt_arg_at(argc, argv, 0));
  xt_array *entries = xt_as_array(xt_url_entries(thisValue));
  xt_value result = xt_array_new(0, NULL);
  for (uint32_t i = 0; i < entries->length; i++) {
    if (xt_url_key_equals(entries->items[i], key)) {
      xt_array_push(result, ((xt_array *)XT_GET_PTR(entries->items[i]))->items[1]);
    }
  }
  return result;
}

static xt_value xt_url_method_has(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value key = xt_to_string(xt_arg_at(argc, argv, 0));
  xt_array *entries = xt_as_array(xt_url_entries(thisValue));
  for (uint32_t i = 0; i < entries->length; i++) {
    if (xt_url_key_equals(entries->items[i], key)) return XT_TRUE;
  }
  return XT_FALSE;
}

static xt_value xt_url_method_delete(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value key = xt_to_string(xt_arg_at(argc, argv, 0));
  xt_array *entries = xt_as_array(xt_url_entries(thisValue));
  uint32_t write = 0;
  for (uint32_t read = 0; read < entries->length; read++) {
    if (!xt_url_key_equals(entries->items[read], key)) entries->items[write++] = entries->items[read];
  }
  entries->length = write;
  xt_url_set_size(thisValue);
  return XT_UNDEFINED;
}

static xt_value xt_url_method_set(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value key = xt_to_string(xt_arg_at(argc, argv, 0));
  xt_value value = xt_to_string(xt_arg_at(argc, argv, 1));
  xt_value entriesValue = xt_url_entries(thisValue);
  xt_array *entries = xt_as_array(entriesValue);
  uint32_t write = 0;
  int replaced = 0;
  for (uint32_t read = 0; read < entries->length; read++) {
    if (xt_url_key_equals(entries->items[read], key)) {
      if (!replaced) {
        ((xt_array *)XT_GET_PTR(entries->items[read]))->items[1] = value;
        entries->items[write++] = entries->items[read];
        replaced = 1;
      }
      continue;
    }
    entries->items[write++] = entries->items[read];
  }
  entries->length = write;
  if (!replaced) xt_url_append_pair(entriesValue, key, value);
  xt_url_set_size(thisValue);
  return XT_UNDEFINED;
}

static xt_value xt_url_method_sort(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_array *entries = xt_as_array(xt_url_entries(thisValue));
  for (uint32_t i = 1; i < entries->length; i++) {
    xt_value current = entries->items[i];
    xt_array *currentPair = xt_as_array(current);
    uint32_t j = i;
    while (j > 0) {
      xt_array *previousPair = xt_as_array(entries->items[j - 1]);
      if (strcmp(xt_string_bytes(xt_as_string(previousPair->items[0])),
                 xt_string_bytes(xt_as_string(currentPair->items[0]))) <= 0)
        break;
      entries->items[j] = entries->items[j - 1];
      j--;
    }
    entries->items[j] = current;
  }
  return XT_UNDEFINED;
}

static xt_value xt_url_method_to_string(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_array *entries = xt_as_array(xt_url_entries(thisValue));
  xt_value out = xt_string_from_cstr("");
  for (uint32_t i = 0; i < entries->length; i++) {
    xt_array *pair = xt_as_array(entries->items[i]);
    if (!pair || pair->length < 2) continue;
    if (i > 0) out = xt_add(out, xt_string_from_cstr("&"));
    out = xt_add(out, xt_url_encode(pair->items[0]));
    out = xt_add(out, xt_string_from_cstr("="));
    out = xt_add(out, xt_url_encode(pair->items[1]));
  }
  return out;
}

static xt_value xt_url_method_for_each(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  if (!XT_IS_FUNCTION(callback)) return XT_UNDEFINED;
  xt_array *entries = xt_as_array(xt_url_entries(thisValue));
  for (uint32_t i = 0; i < entries->length; i++) {
    xt_array *pair = xt_as_array(entries->items[i]);
    if (!pair || pair->length < 2) continue;
    xt_value args[3] = {pair->items[1], pair->items[0], thisValue};
    xt_closure_call(callback, 3, args);
  }
  return XT_UNDEFINED;
}

static xt_value xt_url_method_keys(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_array *entries = xt_as_array(xt_url_entries(thisValue));
  xt_value result = xt_array_new(0, NULL);
  for (uint32_t i = 0; i < entries->length; i++) {
    xt_array *pair = xt_as_array(entries->items[i]);
    if (pair && pair->length >= 1) xt_array_push(result, pair->items[0]);
  }
  return result;
}

static xt_value xt_url_method_values(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_array *entries = xt_as_array(xt_url_entries(thisValue));
  xt_value result = xt_array_new(0, NULL);
  for (uint32_t i = 0; i < entries->length; i++) {
    xt_array *pair = xt_as_array(entries->items[i]);
    if (pair && pair->length >= 2) xt_array_push(result, pair->items[1]);
  }
  return result;
}

static xt_value xt_url_method_entries(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value result = xt_array_new(0, NULL);
  xt_array *entries = xt_as_array(xt_url_entries(thisValue));
  for (uint32_t i = 0; i < entries->length; i++) xt_array_push(result, entries->items[i]);
  return result;
}

static void xt_url_install(xt_value params, const char *name, void *fn) {
  xt_object_set(params, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

xt_value xt_url_search_params_ctor(int32_t argc, xt_value *argv) {
  xt_value params = xt_object_new();
  xt_object_set(params, xt_string_from_cstr("__entries"), xt_array_new(0, NULL));
  xt_object_set(params, xt_string_from_cstr("size"), xt_number(0));
  xt_url_install(params, "append", (void *)xt_url_method_append);
  xt_url_install(params, "delete", (void *)xt_url_method_delete);
  xt_url_install(params, "get", (void *)xt_url_method_get);
  xt_url_install(params, "getAll", (void *)xt_url_method_get_all);
  xt_url_install(params, "has", (void *)xt_url_method_has);
  xt_url_install(params, "set", (void *)xt_url_method_set);
  xt_url_install(params, "sort", (void *)xt_url_method_sort);
  xt_url_install(params, "toString", (void *)xt_url_method_to_string);
  xt_url_install(params, "forEach", (void *)xt_url_method_for_each);
  xt_url_install(params, "keys", (void *)xt_url_method_keys);
  xt_url_install(params, "values", (void *)xt_url_method_values);
  xt_url_install(params, "entries", (void *)xt_url_method_entries);
  xt_url_ctor_from_value(params, xt_arg_at(argc, argv, 0));
  return params;
}
