/*
 * Node.js `crypto` module for xbintsc (self-contained SHA-256).
 *
 * Implements the common hashing surface:
 *   const hash = createHash("sha256").update(data).digest("hex");
 *
 * A hash object is a plain object exposing `update` / `digest` as function
 * properties. Because the generic method dispatcher calls those with the hash
 * object as `this`, `update` can accumulate input and `digest` can finalize it
 * without any special object kind in the core runtime.
 */

#include "rt.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- SHA-256 -------------------------------------------------------------- */

typedef struct {
  uint32_t state[8];
  uint64_t bitlen;
  uint8_t buffer[64];
  size_t buffer_length;
} sha256_ctx;

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_init(sha256_ctx *ctx) {
  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
  ctx->bitlen = 0;
  ctx->buffer_length = 0;
}

static void sha256_compress(sha256_ctx *ctx, const uint8_t *block) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
           ((uint32_t)block[i * 4 + 2] << 8) | ((uint32_t)block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = SHA256_ROTR(w[i - 15], 7) ^ SHA256_ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = SHA256_ROTR(w[i - 2], 17) ^ SHA256_ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
  uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
  for (int i = 0; i < 64; i++) {
    uint32_t s1 = SHA256_ROTR(e, 6) ^ SHA256_ROTR(e, 11) ^ SHA256_ROTR(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t temp1 = h + s1 + ch + sha256_k[i] + w[i];
    uint32_t s0 = SHA256_ROTR(a, 2) ^ SHA256_ROTR(a, 13) ^ SHA256_ROTR(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    ctx->buffer[ctx->buffer_length++] = data[i];
    if (ctx->buffer_length == 64) {
      sha256_compress(ctx, ctx->buffer);
      ctx->bitlen += 512;
      ctx->buffer_length = 0;
    }
  }
}

static void sha256_final(sha256_ctx *ctx, uint8_t *out) {
  size_t i = ctx->buffer_length;
  ctx->buffer[i++] = 0x80;
  if (i > 56) {
    while (i < 64) ctx->buffer[i++] = 0;
    sha256_compress(ctx, ctx->buffer);
    i = 0;
  }
  while (i < 56) ctx->buffer[i++] = 0;
  ctx->bitlen += (uint64_t)ctx->buffer_length * 8;
  for (int j = 0; j < 8; j++) ctx->buffer[56 + j] = (uint8_t)(ctx->bitlen >> (56 - j * 8));
  sha256_compress(ctx, ctx->buffer);
  for (int j = 0; j < 8; j++) {
    out[j * 4] = (uint8_t)(ctx->state[j] >> 24);
    out[j * 4 + 1] = (uint8_t)(ctx->state[j] >> 16);
    out[j * 4 + 2] = (uint8_t)(ctx->state[j] >> 8);
    out[j * 4 + 3] = (uint8_t)(ctx->state[j]);
  }
}

static void sha256_hash(const uint8_t *data, size_t length, uint8_t *out) {
  sha256_ctx ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, data, length);
  sha256_final(&ctx, out);
}

/* -- SHA-1 ---------------------------------------------------------------- */

typedef struct {
  uint32_t state[5];
  uint64_t bitlen;
  uint8_t buffer[64];
  size_t buffer_length;
} sha1_ctx;

#define SHA1_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_init(sha1_ctx *ctx) {
  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xefcdab89;
  ctx->state[2] = 0x98badcfe;
  ctx->state[3] = 0x10325476;
  ctx->state[4] = 0xc3d2e1f0;
  ctx->bitlen = 0;
  ctx->buffer_length = 0;
}

static void sha1_compress(sha1_ctx *ctx, const uint8_t *block) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
           ((uint32_t)block[i * 4 + 2] << 8) | ((uint32_t)block[i * 4 + 3]);
  }
  for (int i = 16; i < 80; i++) {
    w[i] = SHA1_ROTL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }
  uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
  uint32_t e = ctx->state[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) {
      f = (b & c) | ((~b) & d);
      k = 0x5a827999;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ed9eba1;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8f1bbcdc;
    } else {
      f = b ^ c ^ d;
      k = 0xca62c1d6;
    }
    uint32_t temp = SHA1_ROTL(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = SHA1_ROTL(b, 30);
    b = a;
    a = temp;
  }
  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
}

static void sha1_update(sha1_ctx *ctx, const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    ctx->buffer[ctx->buffer_length++] = data[i];
    if (ctx->buffer_length == 64) {
      sha1_compress(ctx, ctx->buffer);
      ctx->bitlen += 512;
      ctx->buffer_length = 0;
    }
  }
}

static void sha1_final(sha1_ctx *ctx, uint8_t *out) {
  size_t i = ctx->buffer_length;
  ctx->buffer[i++] = 0x80;
  if (i > 56) {
    while (i < 64) ctx->buffer[i++] = 0;
    sha1_compress(ctx, ctx->buffer);
    i = 0;
  }
  while (i < 56) ctx->buffer[i++] = 0;
  ctx->bitlen += (uint64_t)ctx->buffer_length * 8;
  for (int j = 0; j < 8; j++) ctx->buffer[56 + j] = (uint8_t)(ctx->bitlen >> (56 - j * 8));
  sha1_compress(ctx, ctx->buffer);
  for (int j = 0; j < 5; j++) {
    out[j * 4] = (uint8_t)(ctx->state[j] >> 24);
    out[j * 4 + 1] = (uint8_t)(ctx->state[j] >> 16);
    out[j * 4 + 2] = (uint8_t)(ctx->state[j] >> 8);
    out[j * 4 + 3] = (uint8_t)(ctx->state[j]);
  }
}

/* Compute `algorithm` over `data`; returns the digest length, or 0 when the
 * algorithm is unsupported. */
static size_t xt_hash_compute(const char *algorithm, const uint8_t *data, size_t length, uint8_t *out) {
  if (algorithm && (strcmp(algorithm, "sha1") == 0 || strcmp(algorithm, "sha-1") == 0)) {
    sha1_ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, length);
    sha1_final(&ctx, out);
    return 20;
  }
  if (!algorithm || strcmp(algorithm, "sha256") == 0 || strcmp(algorithm, "sha-256") == 0) {
    sha256_hash(data, length, out);
    return 32;
  }
  return 0;
}

/* -- node:crypto surface -------------------------------------------------- */

static xt_value hash_data(xt_value hash) {
  xt_value data = xt_object_get(hash, xt_string_from_cstr("_data"));
  if (!data) return xt_string_from_cstr("");
  return data;
}

static const char *hash_algorithm(xt_value hash) {
  xt_value algorithm = xt_object_get(hash, xt_string_from_cstr("_algorithm"));
  const char *name = algorithm ? xt_string_data(xt_to_string(algorithm)) : NULL;
  return name ? name : "sha256";
}

static const char *hash_encoding(xt_value hash, int argc, xt_value *argv) {
  if (argc > 0 && !xt_truthy(xt_is_nullish(argv[0]))) {
    const char *explicitEncoding = xt_string_data(xt_to_string(argv[0]));
    if (explicitEncoding) return explicitEncoding;
  }
  xt_value encoding = xt_object_get(hash, xt_string_from_cstr("_outEncoding"));
  const char *name = encoding ? xt_string_data(xt_to_string(encoding)) : NULL;
  return name ? name : "hex";
}

/* Encode `length` digest bytes into an xt string using `encoding`. */
static xt_value hash_encode(const uint8_t *digest, size_t length, const char *encoding) {
  if (!encoding || strcmp(encoding, "hex") == 0) {
    static const char digits[] = "0123456789abcdef";
    char *hex = (char *)malloc(length * 2 + 1);
    if (!hex) return xt_undefined();
    for (size_t i = 0; i < length; i++) {
      hex[i * 2] = digits[digest[i] >> 4];
      hex[i * 2 + 1] = digits[digest[i] & 0xf];
    }
    hex[length * 2] = 0;
    xt_value result = xt_string_from_cstr(hex);
    free(hex);
    return result;
  }
  if (strcmp(encoding, "base64") == 0) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t outLength = ((length + 2) / 3) * 4;
    char *buffer = (char *)malloc(outLength + 1);
    if (!buffer) return xt_undefined();
    size_t i = 0, j = 0;
    while (i + 2 < length) {
      uint32_t n = ((uint32_t)digest[i] << 16) | ((uint32_t)digest[i + 1] << 8) | (uint32_t)digest[i + 2];
      buffer[j++] = alphabet[(n >> 18) & 63];
      buffer[j++] = alphabet[(n >> 12) & 63];
      buffer[j++] = alphabet[(n >> 6) & 63];
      buffer[j++] = alphabet[n & 63];
      i += 3;
    }
    if (length - i == 1) {
      uint32_t n = (uint32_t)digest[i] << 16;
      buffer[j++] = alphabet[(n >> 18) & 63];
      buffer[j++] = alphabet[(n >> 12) & 63];
      buffer[j++] = '=';
      buffer[j++] = '=';
    } else if (length - i == 2) {
      uint32_t n = ((uint32_t)digest[i] << 16) | ((uint32_t)digest[i + 1] << 8);
      buffer[j++] = alphabet[(n >> 18) & 63];
      buffer[j++] = alphabet[(n >> 12) & 63];
      buffer[j++] = alphabet[(n >> 6) & 63];
      buffer[j++] = '=';
    }
    xt_value result = xt_string_new(buffer, j);
    free(buffer);
    return result;
  }
  return xt_string_new((const char *)digest, length);
}

/* Compute the digest of everything accumulated so far. */
static xt_value hash_finish(xt_value hash, int argc, xt_value *argv) {
  xt_value data = hash_data(hash);
  const char *text = xt_string_data(xt_to_string(data));
  if (!text) text = "";
  uint8_t digest[64];
  size_t length = xt_hash_compute(hash_algorithm(hash), (const uint8_t *)text, strlen(text), digest);
  if (length == 0) return xt_string_from_cstr("");
  return hash_encode(digest, length, hash_encoding(hash, argc, argv));
}

static xt_value xt_hash_update(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (XT_IS_OBJECT(thisValue) && argc > 0) {
    xt_value combined = xt_add(hash_data(thisValue), xt_to_string(argv[0]));
    xt_set(thisValue, xt_string_from_cstr("_data"), combined);
  }
  return thisValue;
}

static xt_value xt_hash_digest(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return hash_finish(thisValue, argc, argv);
}

static xt_value xt_hash_set_encoding(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (XT_IS_OBJECT(thisValue) && argc > 0) {
    xt_set(thisValue, xt_string_from_cstr("_outEncoding"), xt_to_string(argv[0]));
  }
  return thisValue;
}

static xt_value xt_hash_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (XT_IS_OBJECT(thisValue) && argc > 0 && !xt_truthy(xt_is_nullish(argv[0]))) {
    xt_value combined = xt_add(hash_data(thisValue), xt_to_string(argv[0]));
    xt_set(thisValue, xt_string_from_cstr("_data"), combined);
  }
  return xt_bool(1);
}

static xt_value xt_hash_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (XT_IS_OBJECT(thisValue)) {
    xt_value chunk = xt_arg(argc, argv, 0);
    if (argc > 0 && !xt_truthy(xt_is_nullish(chunk))) {
      xt_value combined = xt_add(hash_data(thisValue), xt_to_string(chunk));
      xt_set(thisValue, xt_string_from_cstr("_data"), combined);
    }
    xt_set(thisValue, xt_string_from_cstr("_result"), hash_finish(thisValue, 0, NULL));
    xt_set(thisValue, xt_string_from_cstr("_ended"), xt_bool(1));
  }
  return thisValue;
}

static xt_value xt_hash_read(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  if (!XT_IS_OBJECT(thisValue)) return xt_undefined();
  xt_value result = xt_object_get(thisValue, xt_string_from_cstr("_result"));
  if (xt_truthy(xt_is_nullish(result))) return xt_undefined();
  return result;
}

xt_value xt_crypto_create_hash(int32_t argc, xt_value *argv) {
  const char *algorithm = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : "sha256";
  if (!algorithm) algorithm = "sha256";
  xt_value hash = xt_object_new();
  xt_set(hash, xt_string_from_cstr("_algorithm"), xt_string_from_cstr(algorithm));
  xt_set(hash, xt_string_from_cstr("_data"), xt_string_from_cstr(""));
  xt_set(hash, xt_string_from_cstr("update"), xt_closure_new((void *)xt_hash_update, 0, NULL));
  xt_set(hash, xt_string_from_cstr("digest"), xt_closure_new((void *)xt_hash_digest, 0, NULL));
  xt_set(hash, xt_string_from_cstr("setEncoding"), xt_closure_new((void *)xt_hash_set_encoding, 0, NULL));
  xt_set(hash, xt_string_from_cstr("write"), xt_closure_new((void *)xt_hash_write, 0, NULL));
  xt_set(hash, xt_string_from_cstr("end"), xt_closure_new((void *)xt_hash_end, 0, NULL));
  xt_set(hash, xt_string_from_cstr("read"), xt_closure_new((void *)xt_hash_read, 0, NULL));
  return hash;
}

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
  size_t digest_length = xt_hash_compute(algorithm, &zero, 0, out);
  if (digest_length == 0) return 0;
  uint8_t block[64];
  memset(block, 0, sizeof(block));
  if (key_length > sizeof(block)) {
    uint8_t key_hash[64];
    if (xt_hash_compute(algorithm, key, key_length, key_hash) == 0) return 0;
    memcpy(block, key_hash, digest_length);
  } else {
    memcpy(block, key, key_length);
  }
  uint8_t inner[64 + 4096];
  if (data_length > 4096) return 0;
  for (size_t i = 0; i < sizeof(block); i++) inner[i] = (uint8_t)(block[i] ^ 0x36);
  memcpy(inner + sizeof(block), data, data_length);
  uint8_t inner_hash[64];
  size_t inner_length = xt_hash_compute(algorithm, inner, sizeof(block) + data_length, inner_hash);
  if (inner_length == 0) return 0;
  uint8_t outer[64];
  for (size_t i = 0; i < sizeof(outer); i++) outer[i] = (uint8_t)(block[i] ^ 0x5c);
  uint8_t outer_input[64 + 64];
  memcpy(outer_input, outer, sizeof(outer));
  memcpy(outer_input + sizeof(outer), inner_hash, inner_length);
  if (xt_hash_compute(algorithm, outer_input, sizeof(outer) + inner_length, out) == 0) return 0;
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
  size_t digest_length = xt_hash_compute(algorithm, &zero, 0, probe);
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

