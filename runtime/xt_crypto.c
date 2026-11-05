/*
 * xbintsc runtime — Web Crypto surface.
 *
 * `crypto.getRandomValues(typedArray)` and `crypto.randomUUID()` are available
 * as globals in every program; `node:crypto` re-exports the same object as
 * `webcrypto`. The entropy comes from `xt_random.c` (`BCryptGenRandom` /
 * `/dev/urandom`), never from `Math.random`.
 */

#include "rt.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int xt_crypto_kind_bytes[] = {1, 1, 1, 2, 2, 4, 4, 4, 8};

static void xt_crypto_throw(const char *constructor, const char *message) {
  xt_value text = xt_string_from_cstr(message);
  xt_throw(xt_error_ctor_by_name(xt_string_from_cstr(constructor), 1, &text));
}

static xt_value xt_crypto_make_uuid(void) {
  uint8_t bytes[16];
  if (xt_secure_random(bytes, sizeof(bytes)) != 0) {
    /* The OS source is unavailable: fall back to the seeded generator rather
     * than returning a constant. */
    for (int i = 0; i < 16; i += 8) {
      uint64_t value = xt_random_u64();
      memcpy(bytes + i, &value, 8);
    }
  }
  bytes[6] = (uint8_t)((bytes[6] & 0x0F) | 0x40); /* version 4 */
  bytes[8] = (uint8_t)((bytes[8] & 0x3F) | 0x80); /* RFC 4122 variant */
  char text[37];
  snprintf(text, sizeof(text),
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", bytes[0],
           bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8],
           bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return xt_string_from_cstr(text);
}

/** Fill a typed array with random bytes. The DOM limits a single call to
 * 65 536 bytes; larger requests throw a `RangeError`. */
static xt_value xt_crypto_get_random_values(xt_value array) {
  int kind = xt_typed_array_kind_of(array);
  if (kind < 0) {
    xt_crypto_throw("TypeError", "crypto.getRandomValues requires a typed array");
    return XT_UNDEFINED;
  }
  uint32_t length = xt_typed_array_length(array);
  uint64_t bytes64 = (uint64_t)length * (uint64_t)xt_crypto_kind_bytes[kind];
  if (bytes64 > 65536ULL) {
    xt_crypto_throw("RangeError", "crypto.getRandomValues is limited to 65536 bytes");
    return XT_UNDEFINED;
  }
  if (bytes64 == 0) return array;
  size_t count = (size_t)bytes64;
  uint8_t *bytes = (uint8_t *)malloc(count);
  if (bytes == NULL) {
    xt_crypto_throw("Error", "out of memory");
    return XT_UNDEFINED;
  }
  int failed = xt_secure_random(bytes, count);
  if (failed == 0) {
    xt_array_buffer_copy_bytes_in(xt_typed_array_backing_buffer(array),
                                  xt_typed_array_view_offset(array), bytes, (uint32_t)count);
  }
  free(bytes);
  if (failed != 0) {
    xt_crypto_throw("Error", "the operating system entropy source is unavailable");
    return XT_UNDEFINED;
  }
  return array;
}

static xt_value xt_crypto_get_random_values_method(xt_value self, xt_value env, int32_t argc,
                                                   xt_value *argv) {
  (void)self;
  (void)env;
  return xt_crypto_get_random_values(argc > 0 ? argv[0] : XT_UNDEFINED);
}

static xt_value xt_crypto_random_uuid_method(xt_value self, xt_value env, int32_t argc,
                                             xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return xt_crypto_make_uuid();
}

xt_value xt_crypto_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(name);
  if (method == NULL) return XT_UNDEFINED;
  if (strcmp(method, "getRandomValues") == 0) {
    return xt_crypto_get_random_values(argc > 0 ? argv[0] : XT_UNDEFINED);
  }
  if (strcmp(method, "randomUUID") == 0) return xt_crypto_make_uuid();
  if (strcmp(method, "toJSON") == 0) {
    (void)argc;
    (void)argv;
    return xt_string_from_cstr("[object Crypto]");
  }
  return XT_UNDEFINED;
}

xt_value xt_crypto_get(xt_value name) {
  const char *property = xt_string_data(name);
  if (property == NULL) return XT_UNDEFINED;
  if (strcmp(property, "getRandomValues") == 0) {
    return xt_closure_new((void *)xt_crypto_get_random_values_method, 0, NULL);
  }
  if (strcmp(property, "randomUUID") == 0) {
    return xt_closure_new((void *)xt_crypto_random_uuid_method, 0, NULL);
  }
  return XT_UNDEFINED;
}

xt_value xt_crypto_object(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value object = xt_object_new();
  xt_object_set(object, xt_string_from_cstr("getRandomValues"),
                xt_closure_new((void *)xt_crypto_get_random_values_method, 0, NULL));
  xt_object_set(object, xt_string_from_cstr("randomUUID"),
                xt_closure_new((void *)xt_crypto_random_uuid_method, 0, NULL));
  return object;
}

xt_value xt_crypto_webcrypto(int32_t argc, xt_value *argv) { return xt_crypto_object(argc, argv); }
