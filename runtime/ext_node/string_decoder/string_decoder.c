/*
 * Node.js `string_decoder` module for xbintsc.
 *
 * `StringDecoder` decodes Buffer chunks to a UTF-8 string. Incomplete multi-byte
 * sequences across chunk boundaries are currently passed through unchanged (the
 * module does not yet buffer a split character across `write` calls).
 */

#include "../node_common.h"

int xt_node_is_buffer(xt_value value);
unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength);

static xt_value string_decoder_decode(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  xt_value input = xt_arg(argc, argv, 0);
  if (!xt_node_is_buffer(input)) return xt_string_from_cstr("");
  size_t length = 0;
  unsigned char *bytes = xt_node_buffer_bytes(input, &length);
  return xt_string_new((const char *)bytes, length);
}

xt_value xt_string_decoder_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value decoder = xt_object_new();
  xt_node_set(decoder, "write", xt_closure_new((void *)string_decoder_decode, 0, NULL));
  xt_node_set(decoder, "end", xt_closure_new((void *)string_decoder_decode, 0, NULL));
  return decoder;
}
