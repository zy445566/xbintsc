#include "rt.h"
#include "crypto_hash.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- random and password hashing ----------------------------------------- */

/* `Buffer.alloc`/`from` live in the same extension archive (buffer.c); this
 * module only needs the static dispatcher to hand a byte array back as a
 * Buffer. */
xt_value xt_buffer_static(xt_value name, int32_t argc, xt_value *argv);

static xt_value xt_crypto_buffer(const uint8_t *bytes, size_t length) {
  xt_value values = xt_array_new(0, NULL);
  for (size_t i = 0; i < length; i++) xt_array_push(values, xt_number((double)bytes[i]));
  xt_value name = xt_string_from_cstr("from");
  xt_value args[1] = {values};
  return xt_buffer_static(name, 1, args);
}

static void xt_crypto_fail(const char *constructor, const char *message) {
  xt_value text = xt_string_from_cstr(message);
  xt_throw(xt_error_ctor_by_name(xt_string_from_cstr(constructor), 1, &text));
}

/** Read a string / Buffer / typed array as raw bytes. On success `out` is
 * malloc'ed and must be freed by the caller. */
static int xt_crypto_bytes(xt_value value, uint8_t **out, size_t *length) {
  *out = NULL;
  *length = 0;
  if (XT_IS_STRING(value)) {
    const char *text = xt_string_data(value);
    int32_t count = xt_string_length_value(value);
    if (count < 0) count = 0;
    uint8_t *bytes = (uint8_t *)malloc(count ? (size_t)count : 1);
    if (!bytes) return 0;
    if (count > 0) memcpy(bytes, text, (size_t)count);
    *out = bytes;
    *length = (size_t)count;
    return 1;
  }
  if (xt_is_typed_array(value)) {
    uint32_t view_length = xt_typed_array_length(value);
    /* A `Uint8Array`-style byte view is common; for other element widths the
     * caller's byte count is the view's byte length, which is not exposed.
     * Reading elements keeps this correct for the password/salt use case. */
    uint8_t *bytes = (uint8_t *)malloc(view_length ? view_length : 1);
    if (!bytes) return 0;
    for (uint32_t i = 0; i < view_length; i++) {
      xt_value item = xt_typed_array_read(value, i);
      bytes[i] = (uint8_t)((int)xt_to_number(item) & 0xff);
    }
    *out = bytes;
    *length = view_length;
    return 1;
  }
  if (XT_IS_OBJECT(value)) {
    xt_value length_value = xt_object_get_cstr(value, "length");
    if (!XT_IS_NUMBER(length_value)) return 0;
    size_t count = (size_t)xt_to_number(length_value);
    uint8_t *bytes = (uint8_t *)malloc(count ? count : 1);
    if (!bytes) return 0;
    for (size_t i = 0; i < count; i++) {
      char key[24];
      snprintf(key, sizeof(key), "%zu", i);
      bytes[i] = (uint8_t)((int)xt_to_number(xt_get(value, xt_string_from_cstr(key))) & 0xff);
    }
    *out = bytes;
    *length = count;
    return 1;
  }
  return 0;
}

static xt_value xt_crypto_random_bytes_impl(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  double size_value = xt_to_number(argv[0]);
  if (!(size_value >= 0) || size_value > 2147483647.0) {
    xt_crypto_fail("RangeError", "The value of \"size\" is out of range");
  }
  size_t size = (size_t)size_value;
  uint8_t *bytes = (uint8_t *)malloc(size ? size : 1);
  if (!bytes) xt_crypto_fail("Error", "out of memory");
  if (xt_secure_random(bytes, size) != 0) {
    free(bytes);
    xt_crypto_fail("Error", "the operating system entropy source is unavailable");
  }
  xt_value buffer = xt_crypto_buffer(bytes, size);
  free(bytes);
  return buffer;
}

xt_value xt_crypto_random_bytes(int32_t argc, xt_value *argv) {
  xt_value buffer = xt_crypto_random_bytes_impl(argc, argv);
  if (argc > 1 && XT_IS_FUNCTION(argv[1])) {
    xt_value args[2] = {XT_NULL, buffer};
    xt_call_with_this(argv[1], XT_UNDEFINED, 2, args);
    return xt_undefined();
  }
  return buffer;
}

xt_value xt_crypto_random_uuid(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_crypto_static(xt_string_from_cstr("randomUUID"), 0, NULL);
}

xt_value xt_crypto_random_int(int32_t argc, xt_value *argv) {
  int32_t callback_index = -1;
  if (argc > 0 && XT_IS_FUNCTION(argv[argc - 1])) callback_index = argc - 1;
  int32_t count = callback_index >= 0 ? callback_index : argc;
  double minimum = 0.0;
  double maximum = 0.0;
  if (count == 1) {
    maximum = xt_to_number(argv[0]);
  } else if (count >= 2) {
    minimum = xt_to_number(argv[0]);
    maximum = xt_to_number(argv[1]);
  } else {
    xt_crypto_fail("TypeError", "randomInt requires a range");
  }
  if (!(maximum > minimum)) {
    xt_crypto_fail("RangeError", "The value of \"max\" must be greater than \"min\"");
  }
  double span = maximum - minimum;
  double value = minimum + (double)xt_random_below((uint64_t)span);
  if (callback_index >= 0) {
    xt_value args[2] = {XT_NULL, xt_number(value)};
    xt_call_with_this(argv[callback_index], XT_UNDEFINED, 2, args);
    return xt_undefined();
  }
  return xt_number(value);
}

static int xt_crypto_fill_buffer(xt_value target, size_t offset, size_t size) {
  if (xt_is_typed_array(target)) {
    uint32_t length = xt_typed_array_length(target);
    static const int kind_bytes[] = {1, 1, 1, 2, 2, 4, 4, 4, 8};
    int kind = xt_typed_array_kind_of(target);
    size_t total = kind >= 0 ? (size_t)length * (size_t)kind_bytes[kind] : 0;
    if (offset > total) offset = total;
    if (size > total - offset) size = total - offset;
    if (size == 0) return 1;
    uint8_t *bytes = (uint8_t *)malloc(size);
    if (!bytes) return 0;
    int ok = xt_secure_random(bytes, size) == 0;
    if (ok) {
      xt_array_buffer_copy_bytes_in(xt_typed_array_backing_buffer(target),
                                    xt_typed_array_view_offset(target) + (uint32_t)offset, bytes,
                                    (uint32_t)size);
    }
    free(bytes);
    return ok;
  }
  xt_value length_value = xt_object_get_cstr(target, "length");
  if (!XT_IS_NUMBER(length_value)) return 0;
  size_t total = (size_t)xt_to_number(length_value);
  if (offset > total) offset = total;
  if (size > total - offset) size = total - offset;
  if (size == 0) return 1;
  uint8_t *bytes = (uint8_t *)malloc(size);
  if (!bytes) return 0;
  int ok = xt_secure_random(bytes, size) == 0;
  if (ok) {
    for (size_t i = 0; i < size; i++) {
      char key[24];
      snprintf(key, sizeof(key), "%zu", offset + i);
      xt_set(target, xt_string_from_cstr(key), xt_number((double)bytes[i]));
    }
  }
  free(bytes);
  return ok;
}

xt_value xt_crypto_random_fill_sync(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  xt_value target = argv[0];
  size_t total = 0;
  if (xt_is_typed_array(target)) {
    static const int kind_bytes[] = {1, 1, 1, 2, 2, 4, 4, 4, 8};
    int kind = xt_typed_array_kind_of(target);
    total = kind >= 0 ? (size_t)xt_typed_array_length(target) * (size_t)kind_bytes[kind] : 0;
  } else {
    xt_value length_value = xt_object_get_cstr(target, "length");
    if (!XT_IS_NUMBER(length_value)) {
      xt_crypto_fail("TypeError", "randomFillSync requires a Buffer or TypedArray");
    }
    total = (size_t)xt_to_number(length_value);
  }
  size_t offset = argc > 1 ? (size_t)xt_to_number(argv[1]) : 0;
  size_t size = argc > 2 ? (size_t)xt_to_number(argv[2]) : total - (offset < total ? offset : total);
  if (!xt_crypto_fill_buffer(target, offset, size)) {
    xt_crypto_fail("Error", "the operating system entropy source is unavailable");
  }
  return target;
}

xt_value xt_crypto_random_fill(int32_t argc, xt_value *argv) {
  int32_t callback_index = argc > 1 && XT_IS_FUNCTION(argv[argc - 1]) ? argc - 1 : -1;
  int32_t count = callback_index >= 0 ? callback_index : argc;
  xt_value target = xt_crypto_random_fill_sync(count, argv);
  if (callback_index >= 0) {
    xt_value args[2] = {XT_NULL, target};
    xt_call_with_this(argv[callback_index], XT_UNDEFINED, 2, args);
    return xt_undefined();
  }
  return target;
}

/* -- PBKDF2 -------------------------------------------------------------- */

static size_t xt_hmac(const char *algorithm, const uint8_t *key, size_t key_length,
                      const uint8_t *data, size_t data_length, uint8_t *out) {
  uint8_t zero = 0;
  size_t digest_length = xt_crypto_hash_compute(algorithm, &zero, 0, out);
  if (digest_length == 0) return 0;
  uint8_t block[64];
  memset(block, 0, sizeof(block));
  if (key_length > sizeof(block)) {
    uint8_t key_hash[64];
    if (xt_crypto_hash_compute(algorithm, key, key_length, key_hash) == 0) return 0;
    memcpy(block, key_hash, digest_length);
  } else {
    memcpy(block, key, key_length);
  }
  uint8_t inner[64 + 4096];
  if (data_length > 4096) return 0;
  for (size_t i = 0; i < sizeof(block); i++) inner[i] = (uint8_t)(block[i] ^ 0x36);
  memcpy(inner + sizeof(block), data, data_length);
  uint8_t inner_hash[64];
  size_t inner_length = xt_crypto_hash_compute(algorithm, inner, sizeof(block) + data_length, inner_hash);
  if (inner_length == 0) return 0;
  uint8_t outer[64];
  for (size_t i = 0; i < sizeof(outer); i++) outer[i] = (uint8_t)(block[i] ^ 0x5c);
  uint8_t outer_input[64 + 64];
  memcpy(outer_input, outer, sizeof(outer));
  memcpy(outer_input + sizeof(outer), inner_hash, inner_length);
  if (xt_crypto_hash_compute(algorithm, outer_input, sizeof(outer) + inner_length, out) == 0) return 0;
  return digest_length;
}

xt_value xt_crypto_pbkdf2_sync(int32_t argc, xt_value *argv) {
  if (argc < 4) {
    xt_crypto_fail("TypeError", "pbkdf2Sync requires password, salt, iterations and keylen");
  }
  uint8_t *password = NULL;
  uint8_t *salt = NULL;
  size_t password_length = 0;
  size_t salt_length = 0;
  if (!xt_crypto_bytes(argv[0], &password, &password_length) ||
      !xt_crypto_bytes(argv[1], &salt, &salt_length)) {
    free(password);
    free(salt);
    xt_crypto_fail("TypeError", "password and salt must be strings or Buffers");
  }
  int iterations = (int)xt_to_number(argv[2]);
  size_t key_length = (size_t)xt_to_number(argv[3]);
  const char *algorithm = argc > 4 && !xt_truthy(xt_is_nullish(argv[4]))
                              ? xt_string_data(xt_to_string(argv[4]))
                              : "sha256";
  if (iterations < 1) {
    free(password);
    free(salt);
    xt_crypto_fail("RangeError", "iterations must be a positive number");
  }
  uint8_t zero = 0;
  uint8_t probe[64];
  size_t digest_length = xt_crypto_hash_compute(algorithm, &zero, 0, probe);
  if (digest_length == 0) {
    free(password);
    free(salt);
    xt_crypto_fail("Error", "unsupported PBKDF2 digest");
  }
  uint8_t *output = (uint8_t *)malloc(key_length ? key_length : 1);
  if (!output) {
    free(password);
    free(salt);
    xt_crypto_fail("Error", "out of memory");
  }
  size_t done = 0;
  uint32_t block_index = 1;
  uint8_t *block_input = (uint8_t *)malloc(salt_length + 4);
  if (!block_input) {
    free(password);
    free(salt);
    free(output);
    xt_crypto_fail("Error", "out of memory");
  }
  while (done < key_length) {
    memcpy(block_input, salt, salt_length);
    block_input[salt_length + 0] = (uint8_t)(block_index >> 24);
    block_input[salt_length + 1] = (uint8_t)(block_index >> 16);
    block_input[salt_length + 2] = (uint8_t)(block_index >> 8);
    block_input[salt_length + 3] = (uint8_t)block_index;
    uint8_t u[64];
    uint8_t t[64];
    if (xt_hmac(algorithm, password, password_length, block_input, salt_length + 4, u) == 0) break;
    memcpy(t, u, digest_length);
    for (int round = 1; round < iterations; round++) {
      uint8_t next[64];
      if (xt_hmac(algorithm, password, password_length, u, digest_length, next) == 0) break;
      memcpy(u, next, digest_length);
      for (size_t i = 0; i < digest_length; i++) t[i] ^= u[i];
    }
    size_t copy = key_length - done < digest_length ? key_length - done : digest_length;
    memcpy(output + done, t, copy);
    done += copy;
    block_index++;
  }
  free(block_input);
  free(password);
  free(salt);
  xt_value buffer = xt_crypto_buffer(output, done);
  free(output);
  return buffer;
}

xt_value xt_crypto_pbkdf2(int32_t argc, xt_value *argv) {
  int32_t callback_index = argc > 0 && XT_IS_FUNCTION(argv[argc - 1]) ? argc - 1 : -1;
  int32_t count = callback_index >= 0 ? callback_index : argc;
  xt_value result = xt_crypto_pbkdf2_sync(count, argv);
  if (callback_index >= 0) {
    xt_value args[2] = {XT_NULL, result};
    xt_call_with_this(argv[callback_index], XT_UNDEFINED, 2, args);
    return xt_undefined();
  }
  return result;
}

xt_value xt_crypto_random_fill_callback(int32_t argc, xt_value *argv) {
  return xt_crypto_random_fill(argc, argv);
}



