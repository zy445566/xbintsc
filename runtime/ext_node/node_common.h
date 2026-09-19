/*
 * Shared helpers for the xbintsc Node extension modules.
 *
 * Everything here is header-only so each module translation unit stays
 * self-contained. The most important piece is a small EventEmitter: listeners
 * live in an `__xt_events` property (event name -> array of functions) and
 * events emitted before a listener exists are buffered under `__xt_pending`
 * and flushed when `on`/`addListener` registers. Node's data events therefore
 * work even though xbintsc has no asynchronous event loop of its own.
 */
#ifndef XT_NODE_COMMON_H
#define XT_NODE_COMMON_H

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- small value helpers -------------------------------------------------- */

static inline const char *xt_node_cstr(xt_value value) {
  return xt_string_data(xt_to_string(value));
}

static inline int32_t xt_node_int(xt_value value) {
  return (int32_t)xt_to_number(value);
}

static inline double xt_node_number(xt_value value) { return xt_to_number(value); }

static inline void xt_node_set(xt_value object, const char *key, xt_value value) {
  xt_object_set(object, xt_string_from_cstr(key), value);
}

static inline xt_value xt_node_get(xt_value object, const char *key) {
  return xt_object_get_cstr(object, key);
}

static inline void xt_node_define_method(xt_value object, const char *name, void *fn) {
  xt_node_set(object, name, xt_closure_new(fn, 0, NULL));
}

static inline void xt_node_define_method_env(xt_value object, const char *name, void *fn, int32_t count,
                                             xt_value *env) {
  xt_node_set(object, name, xt_closure_new(fn, count, env));
}

/* -- event emitter -------------------------------------------------------- */

static inline xt_value xt_node_event_map(xt_value target, int create) {
  xt_value map = xt_object_get_cstr(target, "__xt_events");
  if (!XT_IS_OBJECT(map) && create) {
    map = xt_object_new();
    xt_object_set(target, xt_string_from_cstr("__xt_events"), map);
  }
  return map;
}

static inline xt_value xt_node_listeners(xt_value target, const char *event, int create) {
  xt_value map = xt_node_event_map(target, create);
  if (!XT_IS_OBJECT(map)) return XT_UNDEFINED;
  xt_value key = xt_string_from_cstr(event);
  xt_value array = xt_object_get(map, key);
  if (!XT_IS_ARRAY(array) && create) {
    array = xt_array_new(0, NULL);
    xt_object_set(map, key, array);
  }
  return array;
}

static inline xt_value xt_node_pending_map(xt_value target, int create) {
  xt_value map = xt_object_get_cstr(target, "__xt_pending");
  if (!XT_IS_OBJECT(map) && create) {
    map = xt_object_new();
    xt_object_set(target, xt_string_from_cstr("__xt_pending"), map);
  }
  return map;
}

/* Deliver `event` to its listeners, or buffer the arguments for later. */
static inline void xt_node_emit(xt_value target, const char *event, int32_t argc, xt_value *argv) {
  xt_value listeners = xt_node_listeners(target, event, 0);
  if (XT_IS_ARRAY(listeners) && xt_to_number(xt_array_length(listeners)) > 0) {
    int32_t count = (int32_t)xt_to_number(xt_array_length(listeners));
    for (int32_t i = 0; i < count; i++) {
      xt_value fn = xt_array_get(listeners, xt_number((double)i));
      if (XT_IS_FUNCTION(fn)) xt_call_with_this(fn, target, argc, argv);
    }
    return;
  }
  xt_value map = xt_node_pending_map(target, 1);
  xt_value key = xt_string_from_cstr(event);
  xt_value queue = xt_object_get(map, key);
  if (!XT_IS_ARRAY(queue)) {
    queue = xt_array_new(0, NULL);
    xt_object_set(map, key, queue);
  }
  xt_value record = xt_array_new(argc, argv);
  xt_array_push(queue, record);
}

static inline void xt_node_emit0(xt_value target, const char *event) {
  xt_node_emit(target, event, 0, NULL);
}

static inline void xt_node_emit1(xt_value target, const char *event, xt_value arg) {
  xt_node_emit(target, event, 1, &arg);
}

/* Re-dispatch any buffered events once a listener has been attached. */
static inline void xt_node_flush(xt_value target, const char *event) {
  xt_value map = xt_node_pending_map(target, 0);
  if (!XT_IS_OBJECT(map)) return;
  xt_value key = xt_string_from_cstr(event);
  xt_value queue = xt_object_get(map, key);
  if (!XT_IS_ARRAY(queue) || xt_to_number(xt_array_length(queue)) == 0) return;
  xt_value empty = xt_array_new(0, NULL);
  xt_object_set(map, key, empty);
  int32_t count = (int32_t)xt_to_number(xt_array_length(queue));
  for (int32_t i = 0; i < count; i++) {
    xt_value record = xt_array_get(queue, xt_number((double)i));
    int32_t argc = (int32_t)xt_to_number(xt_array_length(record));
    xt_value args[8];
    int32_t limit = argc < 8 ? argc : 8;
    for (int32_t j = 0; j < limit; j++) args[j] = xt_array_get(record, xt_number((double)j));
    xt_node_emit(target, event, limit, args);
  }
}

/* Listener management implemented as native closures (`this` is the target). */
static inline xt_value xt_node_on_impl(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_node_cstr(xt_arg(argc, argv, 0));
  xt_value fn = xt_arg(argc, argv, 1);
  if (!event || !XT_IS_FUNCTION(fn)) return thisValue;
  xt_value array = xt_node_listeners(thisValue, event, 1);
  xt_array_push(array, fn);
  xt_node_flush(thisValue, event);
  return thisValue;
}

static inline xt_value xt_node_remove_listener_impl(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_node_cstr(xt_arg(argc, argv, 0));
  xt_value fn = xt_arg(argc, argv, 1);
  if (!event) return thisValue;
  xt_value array = xt_node_listeners(thisValue, event, 0);
  if (!XT_IS_ARRAY(array)) return thisValue;
  xt_value kept = xt_array_new(0, NULL);
  int32_t count = (int32_t)xt_to_number(xt_array_length(array));
  for (int32_t i = 0; i < count; i++) {
    xt_value item = xt_array_get(array, xt_number((double)i));
    if (!xt_truthy(xt_seq(item, fn))) xt_array_push(kept, item);
  }
  xt_value map = xt_node_event_map(thisValue, 1);
  xt_object_set(map, xt_string_from_cstr(event), kept);
  return thisValue;
}

static inline xt_value xt_node_emit_impl(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_node_cstr(xt_arg(argc, argv, 0));
  if (!event) return xt_bool(0);
  xt_node_emit(thisValue, event, argc > 1 ? argc - 1 : 0, argc > 1 ? argv + 1 : NULL);
  return xt_bool(1);
}

/* Attach the base EventEmitter surface to a prototype object. */
static inline void xt_node_install_emitter(xt_value proto) {
  xt_node_define_method(proto, "on", (void *)xt_node_on_impl);
  xt_node_define_method(proto, "addListener", (void *)xt_node_on_impl);
  xt_node_define_method(proto, "once", (void *)xt_node_on_impl);
  xt_node_define_method(proto, "off", (void *)xt_node_remove_listener_impl);
  xt_node_define_method(proto, "removeListener", (void *)xt_node_remove_listener_impl);
  xt_node_define_method(proto, "removeAllListeners", (void *)xt_node_remove_listener_impl);
  xt_node_define_method(proto, "emit", (void *)xt_node_emit_impl);
}

/* -- byte <-> text helpers ------------------------------------------------ */

static inline int xt_node_hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static inline int xt_node_b64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

/* Decode `text` into a malloc'd byte buffer; caller frees. */
static inline unsigned char *xt_node_decode(const char *text, size_t length, const char *encoding, size_t *outLength) {
  unsigned char *bytes = NULL;
  if (encoding && strcmp(encoding, "base64") == 0) {
    bytes = (unsigned char *)malloc((length / 4 + 1) * 3 + 1);
    if (!bytes) return NULL;
    size_t written = 0;
    int accumulator = 0, bits = 0;
    for (size_t i = 0; i < length; i++) {
      if (text[i] == '=') break;
      int value = xt_node_b64_value(text[i]);
      if (value < 0) continue;
      accumulator = (accumulator << 6) | value;
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        bytes[written++] = (unsigned char)((accumulator >> bits) & 0xff);
      }
    }
    bytes[written] = '\0';
    *outLength = written;
    return bytes;
  }
  if (encoding && (strcmp(encoding, "hex") == 0)) {
    bytes = (unsigned char *)malloc(length / 2 + 1);
    if (!bytes) return NULL;
    size_t written = 0;
    int high = -1;
    for (size_t i = 0; i < length; i++) {
      int value = xt_node_hex_value(text[i]);
      if (value < 0) continue;
      if (high < 0) high = value;
      else {
        bytes[written++] = (unsigned char)((high << 4) | value);
        high = -1;
      }
    }
    bytes[written] = '\0';
    *outLength = written;
    return bytes;
  }
  bytes = (unsigned char *)malloc(length + 1);
  if (!bytes) return NULL;
  if (length > 0) memcpy(bytes, text, length);
  bytes[length] = '\0';
  *outLength = length;
  return bytes;
}

/* Encode bytes as a JavaScript string (default raw UTF-8 / latin1). */
static inline xt_value xt_node_encode(const unsigned char *data, size_t length, const char *encoding) {
  static const char base64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (encoding && strcmp(encoding, "hex") == 0) {
    static const char digits[] = "0123456789abcdef";
    char *buffer = (char *)malloc(length * 2 + 1);
    if (!buffer) return xt_undefined();
    for (size_t i = 0; i < length; i++) {
      buffer[i * 2] = digits[data[i] >> 4];
      buffer[i * 2 + 1] = digits[data[i] & 0x0f];
    }
    xt_value result = xt_string_new(buffer, length * 2);
    free(buffer);
    return result;
  }
  if (encoding && strcmp(encoding, "base64") == 0) {
    size_t outLength = ((length + 2) / 3) * 4;
    char *buffer = (char *)malloc(outLength + 1);
    if (!buffer) return xt_undefined();
    size_t i = 0, j = 0;
    while (i + 2 < length) {
      uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | (uint32_t)data[i + 2];
      buffer[j++] = base64Alphabet[(n >> 18) & 63];
      buffer[j++] = base64Alphabet[(n >> 12) & 63];
      buffer[j++] = base64Alphabet[(n >> 6) & 63];
      buffer[j++] = base64Alphabet[n & 63];
      i += 3;
    }
    if (length - i == 1) {
      uint32_t n = (uint32_t)data[i] << 16;
      buffer[j++] = base64Alphabet[(n >> 18) & 63];
      buffer[j++] = base64Alphabet[(n >> 12) & 63];
      buffer[j++] = '=';
      buffer[j++] = '=';
    } else if (length - i == 2) {
      uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
      buffer[j++] = base64Alphabet[(n >> 18) & 63];
      buffer[j++] = base64Alphabet[(n >> 12) & 63];
      buffer[j++] = base64Alphabet[(n >> 6) & 63];
      buffer[j++] = '=';
    }
    xt_value result = xt_string_new(buffer, j);
    free(buffer);
    return result;
  }
  return xt_string_new((const char *)data, length);
}

/* Accepts a bare encoding string or `{ encoding: "..." }`. */
static inline const char *xt_node_encoding(xt_value options) {
  if (XT_IS_STRING(options)) return xt_string_data(options);
  if (XT_IS_OBJECT(options)) {
    xt_value encoding = xt_object_get_cstr(options, "encoding");
    if (XT_IS_STRING(encoding)) return xt_string_data(encoding);
  }
  return NULL;
}

#endif /* XT_NODE_COMMON_H */
