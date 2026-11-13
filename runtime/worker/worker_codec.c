/*
 * xbintsc runtime — worker message codec.
 *
 * `postMessage` performs a structured clone: the value is serialised in the
 * sender and rebuilt in the receiver, never shared. The wire format is JSON
 * text with a `$` tag on every value JSON cannot represent:
 *
 *   {"$":"u"}                         undefined
 *   {"$":"n","v":"NaN"}               NaN / Infinity / -Infinity / -0
 *   {"$":"i","v":"123"}               BigInt
 *   {"$":"a","id":1,"v":[...]}        Array
 *   {"$":"o","id":1,"v":{...}}        plain object / Error
 *   {"$":"m","id":1,"v":[[k,v],...]}  Map
 *   {"$":"s","id":1,"v":[...]}        Set
 *   {"$":"d","id":1,"v":0}            Date (epoch milliseconds)
 *   {"$":"x","id":1,"v":[src,flags]}  RegExp
 *   {"$":"b","id":1,"v":"<base64>"}   ArrayBuffer
 *   {"$":"t","id":1,"k":"Uint8Array","v":"<base64>"}  typed array
 *   {"$":"r","id":1}                  a repeated reference (shared or cyclic)
 *
 * The `id` is written before the children, so a back-reference always names an
 * already-created object; that is what makes cycles survive the round trip.
 * Typed-array bytes are copied with the buffer helpers, so Float32Array values
 * keep their exact bits (no per-element number round trip).
 */

#include "worker_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- cloning errors ------------------------------------------------------- */

static _Noreturn void xt_wrk_clone_fail(const char *kind) {
  char message[160];
  snprintf(message, sizeof(message), "TypeError: %s could not be cloned.", kind);
  xt_throw(xt_error_from_message(message));
  abort();
}

/* -- reference table ------------------------------------------------------ */

typedef struct {
  xt_value *values;
  unsigned long *ids;
  size_t count;
  size_t capacity;
} xt_wrk_refs;

static unsigned long xt_wrk_refs_find(xt_wrk_refs *refs, xt_value value) {
  for (size_t i = 0; i < refs->count; i++) {
    if (refs->ids[i] != 0 && refs->values[i] == value) return refs->ids[i];
  }
  return 0;
}

static void xt_wrk_refs_add(xt_wrk_refs *refs, xt_value value, unsigned long id) {
  if (refs->count >= refs->capacity) {
    size_t next = refs->capacity ? refs->capacity * 2 : 16;
    xt_value *values = (xt_value *)realloc(refs->values, next * sizeof(xt_value));
    unsigned long *ids = (unsigned long *)realloc(refs->ids, next * sizeof(unsigned long));
    if (!values || !ids) abort();
    refs->values = values;
    refs->ids = ids;
    refs->capacity = next;
  }
  refs->values[refs->count] = value;
  refs->ids[refs->count] = id;
  refs->count++;
}

/* -- encoding ------------------------------------------------------------- */

static void xt_wrk_encode_value(xt_wrk_buffer *buffer, xt_wrk_refs *refs, xt_value value,
                                unsigned long *next_id);

static void xt_wrk_encode_number(xt_wrk_buffer *buffer, double number) {
  if (isnan(number)) {
    xt_wrk_buffer_puts(buffer, "{\"$\":\"n\",\"v\":\"NaN\"}");
    return;
  }
  if (isinf(number)) {
    xt_wrk_buffer_puts(buffer, number < 0 ? "{\"$\":\"n\",\"v\":\"-Infinity\"}"
                                          : "{\"$\":\"n\",\"v\":\"Infinity\"}");
    return;
  }
  if (number == 0.0 && signbit(number)) {
    xt_wrk_buffer_puts(buffer, "{\"$\":\"n\",\"v\":\"-0\"}");
    return;
  }
  char text[64];
  xt_number_to_js_string(number, text, sizeof(text));
  xt_wrk_buffer_puts(buffer, text);
}

static void xt_wrk_encode_typed_array(xt_wrk_buffer *buffer, xt_value value,
                                      unsigned long id) {
  int kind = xt_typed_array_kind_of(value);
  const char *name = xt_typed_array_name_of(kind);
  uint32_t length = xt_typed_array_length(value);
  int width = xt_wrk_width_of_typed_array(name);
  size_t byte_length = (size_t)length * (size_t)width;
  uint8_t *bytes = (uint8_t *)malloc(byte_length ? byte_length : 1);
  if (!bytes) abort();
  xt_array_buffer_copy_bytes_out(xt_typed_array_backing_buffer(value),
                                 xt_typed_array_view_offset(value), bytes, (uint32_t)byte_length);
  xt_wrk_buffer_puts(buffer, "{\"$\":\"t\",\"id\":");
  xt_wrk_buffer_put_uint(buffer, id);
  xt_wrk_buffer_puts(buffer, ",\"k\":");
  xt_wrk_buffer_put_string(buffer, name ? name : "Uint8Array", name ? strlen(name) : 10);
  xt_wrk_buffer_puts(buffer, ",\"v\":\"");
  xt_wrk_buffer_put_base64(buffer, bytes, byte_length);
  xt_wrk_buffer_puts(buffer, "\"}");
  free(bytes);
}

static void xt_wrk_encode_object(xt_wrk_buffer *buffer, xt_wrk_refs *refs, xt_value value,
                                 unsigned long id, unsigned long *next_id) {
  xt_value keys = xt_object_keys(value);
  xt_array *list = xt_as_array(keys);
  xt_wrk_buffer_puts(buffer, "{\"$\":\"o\",\"id\":");
  xt_wrk_buffer_put_uint(buffer, id);
  xt_wrk_buffer_puts(buffer, ",\"v\":{");
  if (list) {
    for (uint32_t i = 0; i < list->length; i++) {
      xt_value key = list->items[i];
      xt_value item = xt_object_get(value, key);
      if (XT_IS_FUNCTION(item)) xt_wrk_clone_fail("function");
      xt_string *text = xt_as_string(key);
      if (i > 0) xt_wrk_buffer_putc(buffer, ',');
      xt_wrk_buffer_put_string(buffer, xt_string_bytes(text), (size_t)text->length);
      xt_wrk_buffer_putc(buffer, ':');
      xt_wrk_encode_value(buffer, refs, item, next_id);
    }
  }
  xt_wrk_buffer_puts(buffer, "}}");
}

static void xt_wrk_encode_value(xt_wrk_buffer *buffer, xt_wrk_refs *refs, xt_value value,
                                unsigned long *next_id) {
  if (value == XT_UNDEFINED) {
    xt_wrk_buffer_puts(buffer, "{\"$\":\"u\"}");
    return;
  }
  if (value == XT_NULL) {
    xt_wrk_buffer_puts(buffer, "null");
    return;
  }
  if (XT_IS_BOOL(value)) {
    xt_wrk_buffer_puts(buffer, value == XT_TRUE ? "true" : "false");
    return;
  }
  if (XT_IS_NUMBER(value)) {
    xt_wrk_encode_number(buffer, xt_to_double(value));
    return;
  }
  if (XT_IS_STRING(value)) {
    xt_string *text = xt_as_string(value);
    const char *bytes = xt_string_bytes(text);
    xt_wrk_buffer_put_string(buffer, bytes, (size_t)text->length);
    return;
  }
  if (XT_IS_BIGINT(value)) {
    xt_value decimal = xt_bigint_to_decimal(value);
    xt_string *text = xt_as_string(decimal);
    xt_wrk_buffer_puts(buffer, "{\"$\":\"i\",\"v\":");
    xt_wrk_buffer_put_string(buffer, xt_string_bytes(text), (size_t)text->length);
    xt_wrk_buffer_putc(buffer, '}');
    return;
  }
  if (XT_IS_FUNCTION(value)) xt_wrk_clone_fail("function");
  if (xt_is_symbol(value)) xt_wrk_clone_fail("symbol");

  unsigned long existing = xt_wrk_refs_find(refs, value);
  if (existing != 0) {
    xt_wrk_buffer_puts(buffer, "{\"$\":\"r\",\"id\":");
    xt_wrk_buffer_put_uint(buffer, existing);
    xt_wrk_buffer_putc(buffer, '}');
    return;
  }
  unsigned long id = (*next_id)++;
  xt_wrk_refs_add(refs, value, id);

  if (xt_typed_array_kind_of(value) >= 0) {
    xt_wrk_encode_typed_array(buffer, value, id);
    return;
  }
  if (xt_is_array_buffer(value)) {
    uint32_t length = xt_array_buffer_byte_length(value);
    uint8_t *bytes = (uint8_t *)malloc(length ? length : 1);
    if (!bytes) abort();
    xt_array_buffer_copy_bytes_out(value, 0, bytes, length);
    xt_wrk_buffer_puts(buffer, "{\"$\":\"b\",\"id\":");
    xt_wrk_buffer_put_uint(buffer, id);
    xt_wrk_buffer_puts(buffer, ",\"v\":\"");
    xt_wrk_buffer_put_base64(buffer, bytes, length);
    xt_wrk_buffer_puts(buffer, "\"}");
    free(bytes);
    return;
  }
  if (XT_IS_ARRAY(value)) {
    xt_array *array = xt_as_array(value);
    xt_wrk_buffer_puts(buffer, "{\"$\":\"a\",\"id\":");
    xt_wrk_buffer_put_uint(buffer, id);
    xt_wrk_buffer_puts(buffer, ",\"v\":[");
    if (array) {
      for (uint32_t i = 0; i < array->length; i++) {
        if (i > 0) xt_wrk_buffer_putc(buffer, ',');
        xt_wrk_encode_value(buffer, refs, array->items[i], next_id);
      }
    }
    xt_wrk_buffer_puts(buffer, "]}");
    return;
  }
  if (!XT_IS_OBJECT(value)) xt_wrk_clone_fail("value");

  xt_object *object = (xt_object *)XT_GET_PTR(value);
  if (object->header.kind == XT_OBJECT_KIND_MAP) {
    xt_map *map = (xt_map *)object;
    xt_wrk_buffer_puts(buffer, "{\"$\":\"m\",\"id\":");
    xt_wrk_buffer_put_uint(buffer, id);
    xt_wrk_buffer_puts(buffer, ",\"v\":[");
    for (uint32_t i = 0; i < map->count; i++) {
      if (i > 0) xt_wrk_buffer_putc(buffer, ',');
      xt_wrk_buffer_putc(buffer, '[');
      xt_wrk_encode_value(buffer, refs, map->keys[i], next_id);
      xt_wrk_buffer_putc(buffer, ',');
      xt_wrk_encode_value(buffer, refs, map->values[i], next_id);
      xt_wrk_buffer_putc(buffer, ']');
    }
    xt_wrk_buffer_puts(buffer, "]}");
    return;
  }
  if (object->header.kind == XT_OBJECT_KIND_SET) {
    xt_set_object *set = (xt_set_object *)object;
    xt_wrk_buffer_puts(buffer, "{\"$\":\"s\",\"id\":");
    xt_wrk_buffer_put_uint(buffer, id);
    xt_wrk_buffer_puts(buffer, ",\"v\":[");
    for (uint32_t i = 0; i < set->count; i++) {
      if (i > 0) xt_wrk_buffer_putc(buffer, ',');
      xt_wrk_encode_value(buffer, refs, set->values[i], next_id);
    }
    xt_wrk_buffer_puts(buffer, "]}");
    return;
  }
  if (object->header.kind == XT_OBJECT_KIND_DATE) {
    double time = ((xt_date *)object)->time;
    xt_wrk_buffer_puts(buffer, "{\"$\":\"d\",\"id\":");
    xt_wrk_buffer_put_uint(buffer, id);
    xt_wrk_buffer_puts(buffer, ",\"v\":");
    xt_wrk_encode_number(buffer, time);
    xt_wrk_buffer_putc(buffer, '}');
    return;
  }
  if (object->header.kind == XT_OBJECT_KIND_REGEXP) {
    /* `source`/`flags` are accessors on the RegExp prototype, and the generic
     * object lookup refuses non-`OBJECT` representations, so read them through
     * `xt_get` (which knows the RegExp layout). */
    xt_value source = xt_get(value, xt_string_from_cstr("source"));
    xt_value flags = xt_get(value, xt_string_from_cstr("flags"));
    xt_wrk_buffer_puts(buffer, "{\"$\":\"x\",\"id\":");
    xt_wrk_buffer_put_uint(buffer, id);
    xt_wrk_buffer_puts(buffer, ",\"v\":[");
    xt_wrk_encode_value(buffer, refs, source, next_id);
    xt_wrk_buffer_putc(buffer, ',');
    xt_wrk_encode_value(buffer, refs, flags, next_id);
    xt_wrk_buffer_puts(buffer, "]}");
    return;
  }
  if (object->header.kind == XT_OBJECT_KIND_PROMISE) xt_wrk_clone_fail("Promise");
  if (object->header.kind == XT_OBJECT_KIND_WEAK_MAP) xt_wrk_clone_fail("WeakMap");
  if (object->header.kind == XT_OBJECT_KIND_WEAK_SET) xt_wrk_clone_fail("WeakSet");
  if (object->header.kind == XT_OBJECT_KIND_WEAK_REF) xt_wrk_clone_fail("WeakRef");
  if (object->header.kind == XT_OBJECT_KIND_GENERATOR) xt_wrk_clone_fail("generator");
  if (object->header.kind == XT_OBJECT_KIND_ITERATOR) xt_wrk_clone_fail("iterator");
  if (object->header.kind == XT_OBJECT_KIND_ASYNC) xt_wrk_clone_fail("async function");
  /* Host handles are not cloneable, matching the platform algorithm. */
  if (xt_object_get_cstr(value, "__xt_port") != XT_UNDEFINED) xt_wrk_clone_fail("MessagePort");
  if (xt_object_get_cstr(value, "__xt_worker") != XT_UNDEFINED) xt_wrk_clone_fail("Worker");
  xt_wrk_encode_object(buffer, refs, value, id, next_id);
}

char *xt_wrk_encode(xt_value value, size_t *length) {
  xt_wrk_buffer buffer = {NULL, 0, 0};
  xt_wrk_refs refs = {NULL, NULL, 0, 0};
  unsigned long next_id = 1;
  xt_wrk_encode_value(&buffer, &refs, value, &next_id);
  free(refs.values);
  free(refs.ids);
  if (!buffer.data) xt_wrk_buffer_putc(&buffer, 'n');
  *length = buffer.length;
  return buffer.data;
}

/* -- decoding ------------------------------------------------------------- */

typedef struct {
  xt_value *values;
  size_t count;
  size_t capacity;
} xt_wrk_decoded;

static void xt_wrk_decoded_put(xt_wrk_decoded *decoded, unsigned long id, xt_value value) {
  if (id == 0) return;
  if (id >= decoded->capacity) {
    size_t next = decoded->capacity ? decoded->capacity : 16;
    while (next <= id) next *= 2;
    xt_value *values = (xt_value *)realloc(decoded->values, next * sizeof(xt_value));
    if (!values) abort();
    decoded->values = values;
    decoded->capacity = next;
  }
  decoded->values[id] = value;
  if (decoded->count <= id) decoded->count = id + 1;
}

static xt_value xt_wrk_decoded_get(xt_wrk_decoded *decoded, unsigned long id) {
  if (id == 0 || id >= decoded->count) return XT_UNDEFINED;
  return decoded->values[id];
}

static unsigned long xt_wrk_field_id(xt_value node) {
  return (unsigned long)xt_to_number(xt_object_get_cstr(node, "id"));
}

static const char *xt_wrk_field_string(xt_value node, const char *name) {
  xt_value value = xt_object_get_cstr(node, name);
  return XT_IS_STRING(value) ? xt_string_data(value) : NULL;
}

static int xt_wrk_node_tag(xt_value node, const char **out) {
  *out = NULL;
  if (!XT_IS_OBJECT(node)) return 0;
  xt_value tag = xt_object_get_cstr(node, "$");
  if (!XT_IS_STRING(tag)) return 0;
  *out = xt_string_data(tag);
  return 1;
}

static xt_value xt_wrk_decode_value(xt_value node, xt_wrk_decoded *decoded);

static xt_value xt_wrk_decode_array(xt_value node, xt_wrk_decoded *decoded, unsigned long id) {
  xt_value array = xt_array_new(0, NULL);
  xt_wrk_decoded_put(decoded, id, array);
  xt_value items = xt_object_get_cstr(node, "v");
  xt_array *list = xt_as_array(items);
  if (!list) return array;
  for (uint32_t i = 0; i < list->length; i++) {
    xt_array_push(array, xt_wrk_decode_value(list->items[i], decoded));
  }
  return array;
}

static xt_value xt_wrk_decode_map(xt_value node, xt_wrk_decoded *decoded, unsigned long id) {
  xt_value map = xt_map_ctor(0, NULL);
  xt_wrk_decoded_put(decoded, id, map);
  xt_array *entries = xt_as_array(xt_object_get_cstr(node, "v"));
  if (!entries) return map;
  for (uint32_t i = 0; i < entries->length; i++) {
    xt_array *pair = xt_as_array(entries->items[i]);
    if (!pair || pair->length < 2) continue;
    xt_value args[2] = {xt_wrk_decode_value(pair->items[0], decoded),
                        xt_wrk_decode_value(pair->items[1], decoded)};
    xt_call_method(map, xt_string_from_cstr("set"), 2, args);
  }
  return map;
}

static xt_value xt_wrk_decode_set(xt_value node, xt_wrk_decoded *decoded, unsigned long id) {
  xt_value set = xt_set_ctor(0, NULL);
  xt_wrk_decoded_put(decoded, id, set);
  xt_array *items = xt_as_array(xt_object_get_cstr(node, "v"));
  if (!items) return set;
  for (uint32_t i = 0; i < items->length; i++) {
    xt_value argument = xt_wrk_decode_value(items->items[i], decoded);
    xt_call_method(set, xt_string_from_cstr("add"), 1, &argument);
  }
  return set;
}

static xt_value xt_wrk_decode_object(xt_value node, xt_wrk_decoded *decoded, unsigned long id) {
  xt_value object = xt_object_new();
  xt_wrk_decoded_put(decoded, id, object);
  xt_value bag = xt_object_get_cstr(node, "v");
  if (!XT_IS_OBJECT(bag)) return object;
  xt_value keys = xt_object_keys(bag);
  xt_array *list = xt_as_array(keys);
  if (!list) return object;
  for (uint32_t i = 0; i < list->length; i++) {
    xt_value key = list->items[i];
    xt_object_set(object, key, xt_wrk_decode_value(xt_object_get(bag, key), decoded));
  }
  return object;
}

static xt_value xt_wrk_decode_bytes(xt_value node, unsigned long id, xt_wrk_decoded *decoded,
                                    int typed) {
  const char *text = xt_wrk_field_string(node, "v");
  size_t length = 0;
  uint8_t *bytes = text ? xt_wrk_decode_base64(text, strlen(text), &length) : NULL;
  if (typed) {
    const char *name = xt_wrk_field_string(node, "k");
    int width = xt_wrk_width_of_typed_array(name);
    if (width <= 0 || length % (size_t)width != 0) {
      if (bytes) free(bytes);
      xt_throw(xt_error_from_message("TypeError: malformed typed array in worker message"));
      return XT_UNDEFINED;
    }
    xt_value name_value = xt_string_from_cstr(name);
    xt_value count = xt_number((double)(length / (size_t)width));
    xt_value array = xt_typed_array_ctor_by_name(name_value, 1, &count);
    xt_array_buffer_copy_bytes_in(xt_typed_array_backing_buffer(array),
                                  xt_typed_array_view_offset(array), bytes, (uint32_t)length);
    if (bytes) free(bytes);
    xt_wrk_decoded_put(decoded, id, array);
    return array;
  }
  xt_value buffer = xt_array_buffer_alloc((uint32_t)length);
  if (length > 0) xt_array_buffer_copy_bytes_in(buffer, 0, bytes, (uint32_t)length);
  if (bytes) free(bytes);
  xt_wrk_decoded_put(decoded, id, buffer);
  return buffer;
}

static xt_value xt_wrk_decode_value(xt_value node, xt_wrk_decoded *decoded) {
  const char *tag = NULL;
  if (!xt_wrk_node_tag(node, &tag)) {
    if (XT_IS_ARRAY(node)) {
      xt_array *list = xt_as_array(node);
      xt_value array = xt_array_new(0, NULL);
      for (uint32_t i = 0; i < list->length; i++) {
        xt_array_push(array, xt_wrk_decode_value(list->items[i], decoded));
      }
      return array;
    }
    if (XT_IS_OBJECT(node)) return xt_wrk_decode_object(node, decoded, 0);
    return node;
  }
  if (strcmp(tag, "u") == 0) return XT_UNDEFINED;
  unsigned long id = xt_wrk_field_id(node);
  if (strcmp(tag, "n") == 0) {
    const char *text = xt_wrk_field_string(node, "v");
    if (!text) return xt_undefined();
    if (strcmp(text, "NaN") == 0) return xt_number(0.0 / 0.0);
    if (strcmp(text, "Infinity") == 0) return xt_number(1.0 / 0.0);
    if (strcmp(text, "-Infinity") == 0) return xt_number(-1.0 / 0.0);
    if (strcmp(text, "-0") == 0) return xt_number(-0.0);
    return xt_number(xt_to_number(xt_string_from_cstr(text)));
  }
  if (strcmp(tag, "i") == 0) {
    const char *text = xt_wrk_field_string(node, "v");
    return text ? xt_bigint_from_string(text, strlen(text)) : XT_UNDEFINED;
  }
  if (strcmp(tag, "r") == 0) return xt_wrk_decoded_get(decoded, id);
  if (strcmp(tag, "a") == 0) return xt_wrk_decode_array(node, decoded, id);
  if (strcmp(tag, "o") == 0) return xt_wrk_decode_object(node, decoded, id);
  if (strcmp(tag, "m") == 0) return xt_wrk_decode_map(node, decoded, id);
  if (strcmp(tag, "s") == 0) return xt_wrk_decode_set(node, decoded, id);
  if (strcmp(tag, "b") == 0) return xt_wrk_decode_bytes(node, id, decoded, 0);
  if (strcmp(tag, "t") == 0) return xt_wrk_decode_bytes(node, id, decoded, 1);
  if (strcmp(tag, "d") == 0) {
    xt_value time = xt_wrk_decode_value(xt_object_get_cstr(node, "v"), decoded);
    xt_value date = xt_date_ctor(1, &time);
    xt_wrk_decoded_put(decoded, id, date);
    return date;
  }
  if (strcmp(tag, "x") == 0) {
    xt_array *parts = xt_as_array(xt_object_get_cstr(node, "v"));
    if (!parts || parts->length < 2) return xt_undefined();
    xt_value args[2] = {xt_wrk_decode_value(parts->items[0], decoded),
                        xt_wrk_decode_value(parts->items[1], decoded)};
    xt_value regexp = xt_regexp_ctor(2, args);
    xt_wrk_decoded_put(decoded, id, regexp);
    return regexp;
  }
  xt_throw(xt_error_from_message("TypeError: malformed worker message"));
  return XT_UNDEFINED;
}

xt_value xt_wrk_decode(const char *bytes, size_t length) {
  xt_value text = xt_string_new(bytes, length);
  xt_value node = xt_json_parse(1, &text);
  xt_wrk_decoded decoded = {NULL, 0, 0};
  xt_value value = xt_wrk_decode_value(node, &decoded);
  free(decoded.values);
  return value;
}
