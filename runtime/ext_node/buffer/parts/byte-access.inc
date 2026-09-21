/* -- byte access ---------------------------------------------------------- */

static char *xt_buffer_key(uint32_t index, char *buffer, size_t size) {
  snprintf(buffer, size, "%u", index);
  return buffer;
}

static uint32_t xt_buffer_length(xt_value buffer) {
  double length = xt_to_number(xt_object_get_cstr(buffer, "length"));
  if (length < 0) return 0;
  return (uint32_t)length;
}

static unsigned char xt_buffer_byte(xt_value buffer, uint32_t index) {
  char key[24];
  xt_value value = xt_object_get_cstr(buffer, xt_buffer_key(index, key, sizeof(key)));
  return (unsigned char)((int32_t)xt_to_number(value) & 0xff);
}

static void xt_buffer_put(xt_value buffer, uint32_t index, unsigned char byte) {
  char key[24];
  xt_object_set(buffer, xt_string_from_cstr(xt_buffer_key(index, key, sizeof(key))), xt_number((double)byte));
}

static xt_value xt_buffer_new(uint32_t length, const unsigned char *data) {
  xt_value buffer = xt_object_new_with_proto(xt_buffer_proto());
  for (uint32_t i = 0; i < length; i++) xt_buffer_put(buffer, i, data ? data[i] : 0);
  xt_node_set(buffer, "length", xt_number((double)length));
  return buffer;
}

static int xt_buffer_is(xt_value value) {
  if (!XT_IS_OBJECT(value)) return 0;
  return xt_truthy(xt_seq(xt_object_get_prototype(value), xt_buffer_proto()));
}
