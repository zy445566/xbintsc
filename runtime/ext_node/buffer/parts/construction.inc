/* -- construction --------------------------------------------------------- */

static xt_value xt_buffer_from_bytes(const unsigned char *data, size_t length) {
  return xt_buffer_new((uint32_t)length, data);
}

static xt_value xt_buffer_from(xt_value value, const char *encoding) {
  if (xt_buffer_is(value)) {
    uint32_t length = xt_buffer_length(value);
    unsigned char *copy = (unsigned char *)malloc(length ? length : 1);
    for (uint32_t i = 0; i < length; i++) copy[i] = xt_buffer_byte(value, i);
    xt_value result = xt_buffer_new(length, copy);
    free(copy);
    return result;
  }
  if (XT_IS_ARRAY(value)) {
    uint32_t length = (uint32_t)xt_to_number(xt_array_length(value));
    xt_value result = xt_buffer_new(length, NULL);
    for (uint32_t i = 0; i < length; i++) {
      xt_value item = xt_array_get(value, xt_number((double)i));
      xt_buffer_put(result, i, (unsigned char)((int32_t)xt_to_number(item) & 0xff));
    }
    return result;
  }
  xt_value text = xt_to_string(value);
  xt_string *string = xt_as_string(text);
  size_t decoded = 0;
  unsigned char *bytes = xt_node_decode(string->data, string->length, encoding, &decoded);
  if (!bytes) return xt_buffer_new(0, NULL);
  xt_value result = xt_buffer_from_bytes(bytes, decoded);
  free(bytes);
  return result;
}

static xt_value xt_buffer_from_args(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_buffer_new(0, NULL);
  const char *encoding = argc > 1 ? xt_node_encoding(argv[1]) : NULL;
  return xt_buffer_from(argv[0], encoding);
}

static xt_value xt_buffer_alloc(uint32_t size, xt_value fill, const char *encoding) {
  xt_value buffer = xt_buffer_new(size, NULL);
  if (XT_IS_UNDEFINED(fill)) return buffer;
  if (XT_IS_NUMBER(fill)) {
    unsigned char byte = (unsigned char)((int32_t)xt_to_number(fill) & 0xff);
    for (uint32_t i = 0; i < size; i++) xt_buffer_put(buffer, i, byte);
    return buffer;
  }
  xt_value text = xt_to_string(fill);
  xt_string *string = xt_as_string(text);
  size_t decoded = 0;
  unsigned char *bytes = xt_node_decode(string->data, string->length, encoding, &decoded);
  if (!bytes || decoded == 0) {
    free(bytes);
    return buffer;
  }
  for (uint32_t i = 0; i < size; i++) xt_buffer_put(buffer, i, bytes[i % decoded]);
  free(bytes);
  return buffer;
}
