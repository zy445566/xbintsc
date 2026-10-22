/*
 * xbintsc runtime — TextEncoder / TextDecoder.
 *
 * Strings are stored as UTF-8 (lone surrogate halves as WTF-8), so encoding is
 * a byte copy and decoding is a byte copy back. The constructors return plain
 * objects holding the method closures, which is the same shape the rest of the
 * runtime uses for first-class methods.
 */

#include "rt_internal.h"

#include <stdlib.h>

static xt_value xt_text_encoder_encode(xt_value thisValue, xt_value env, int32_t argc,
                                       xt_value *argv) {
  (void)thisValue;
  (void)env;
  xt_value input = xt_arg_at(argc, argv, 0);
  /* `encode()` with no argument encodes the empty string, like the web API. */
  xt_value text = input == XT_UNDEFINED ? xt_string_from_cstr("") : xt_to_string(input);
  xt_string *string = xt_as_string(text);
  const char *bytes = xt_string_bytes(string);
  xt_value length = xt_number((double)string->length);
  xt_value array = xt_typed_array_ctor_by_name(xt_string_from_cstr("Uint8Array"), 1, &length);
  for (uint32_t i = 0; i < string->length; i++) {
    xt_set(array, xt_number((double)i), xt_number((double)(unsigned char)bytes[i]));
  }
  return array;
}

static xt_value xt_text_decoder_decode(xt_value thisValue, xt_value env, int32_t argc,
                                       xt_value *argv) {
  (void)thisValue;
  (void)env;
  xt_value input = xt_arg_at(argc, argv, 0);
  if (input == XT_UNDEFINED) return xt_string_from_cstr("");
  uint32_t length = (uint32_t)xt_to_number(xt_get(input, xt_string_from_cstr("length")));
  char *bytes = (char *)malloc(length == 0 ? 1 : length);
  if (!bytes) abort();
  for (uint32_t i = 0; i < length; i++) {
    double byte = xt_to_number(xt_get(input, xt_number((double)i)));
    bytes[i] = (char)((int)byte & 0xff);
  }
  xt_value result = xt_string_new(bytes, length);
  free(bytes);
  return result;
}

xt_value xt_text_encoder_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value encoder = xt_object_new();
  xt_object_set(encoder, xt_string_from_cstr("encode"),
                xt_closure_new((void *)xt_text_encoder_encode, 0, NULL));
  xt_object_set(encoder, xt_string_from_cstr("encoding"), xt_string_from_cstr("utf-8"));
  return encoder;
}

xt_value xt_text_decoder_ctor(int32_t argc, xt_value *argv) {
  (void)argv;
  xt_value decoder = xt_object_new();
  xt_object_set(decoder, xt_string_from_cstr("decode"),
                xt_closure_new((void *)xt_text_decoder_decode, 0, NULL));
  /* The only supported encoding is UTF-8; report the canonical label even when
   * the caller passed "utf8" (as TextDecoder does). */
  (void)argc;
  xt_object_set(decoder, xt_string_from_cstr("encoding"), xt_string_from_cstr("utf-8"));
  return decoder;
}
