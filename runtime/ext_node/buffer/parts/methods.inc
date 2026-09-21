/* -- instance methods ----------------------------------------------------- */

static xt_value xt_buffer_method_to_string(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *encoding = argc > 0 ? xt_node_encoding(argv[0]) : NULL;
  uint32_t length = xt_buffer_length(thisValue);
  uint32_t start = argc > 1 ? (uint32_t)xt_to_number(argv[1]) : 0;
  uint32_t end = argc > 2 ? (uint32_t)xt_to_number(argv[2]) : length;
  if (end > length) end = length;
  if (start > end) start = end;
  unsigned char *bytes = (unsigned char *)malloc(end - start + 1);
  for (uint32_t i = start; i < end; i++) bytes[i - start] = xt_buffer_byte(thisValue, i);
  xt_value result = xt_node_encode(bytes, end - start, encoding);
  free(bytes);
  return result;
}

static xt_value xt_buffer_method_to_json(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value object = xt_object_new();
  xt_value type = xt_string_from_cstr("Buffer");
  xt_object_set(object, xt_string_from_cstr("type"), type);
  xt_value data = xt_array_new(0, NULL);
  uint32_t length = xt_buffer_length(thisValue);
  for (uint32_t i = 0; i < length; i++) xt_array_push(data, xt_number((double)xt_buffer_byte(thisValue, i)));
  xt_object_set(object, xt_string_from_cstr("data"), data);
  return object;
}

static xt_value xt_buffer_method_slice(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  uint32_t length = xt_buffer_length(thisValue);
  int32_t start = argc > 0 ? (int32_t)xt_to_number(argv[0]) : 0;
  int32_t end = argc > 1 ? (int32_t)xt_to_number(argv[1]) : (int32_t)length;
  if (start < 0) start += (int32_t)length;
  if (end < 0) end += (int32_t)length;
  if (start < 0) start = 0;
  if (end > (int32_t)length) end = (int32_t)length;
  if (end < start) end = start;
  unsigned char *bytes = (unsigned char *)malloc((size_t)(end - start) + 1);
  for (int32_t i = start; i < end; i++) bytes[i - start] = xt_buffer_byte(thisValue, (uint32_t)i);
  xt_value result = xt_buffer_from_bytes(bytes, (size_t)(end - start));
  free(bytes);
  return result;
}

static xt_value xt_buffer_method_equals(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value other = xt_arg_at(argc, argv, 0);
  if (!xt_buffer_is(other)) return xt_bool(0);
  uint32_t length = xt_buffer_length(thisValue);
  if (length != xt_buffer_length(other)) return xt_bool(0);
  for (uint32_t i = 0; i < length; i++) {
    if (xt_buffer_byte(thisValue, i) != xt_buffer_byte(other, i)) return xt_bool(0);
  }
  return xt_bool(1);
}

static xt_value xt_buffer_method_compare(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value other = xt_arg_at(argc, argv, 0);
  uint32_t la = xt_buffer_length(thisValue), lb = xt_buffer_length(other);
  uint32_t n = la < lb ? la : lb;
  for (uint32_t i = 0; i < n; i++) {
    unsigned char x = xt_buffer_byte(thisValue, i), y = xt_buffer_byte(other, i);
    if (x != y) return xt_number(x < y ? -1 : 1);
  }
  return xt_number(la < lb ? -1 : la > lb ? 1 : 0);
}

static xt_value xt_buffer_method_copy(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value target = xt_arg_at(argc, argv, 0);
  if (!xt_buffer_is(target)) return xt_number(0);
  uint32_t targetStart = argc > 1 ? (uint32_t)xt_to_number(argv[1]) : 0;
  uint32_t sourceStart = argc > 2 ? (uint32_t)xt_to_number(argv[2]) : 0;
  uint32_t sourceEnd = argc > 3 ? (uint32_t)xt_to_number(argv[3]) : xt_buffer_length(thisValue);
  uint32_t targetLength = xt_buffer_length(target);
  uint32_t written = 0;
  for (uint32_t i = sourceStart; i < sourceEnd && targetStart + written < targetLength; i++) {
    xt_buffer_put(target, targetStart + written, xt_buffer_byte(thisValue, i));
    written++;
  }
  return xt_number((double)written);
}

static xt_value xt_buffer_method_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value text = xt_to_string(xt_arg_at(argc, argv, 0));
  xt_string *string = xt_as_string(text);
  uint32_t offset = argc > 1 ? (uint32_t)xt_to_number(argv[1]) : 0;
  const char *encoding = argc > 3 ? xt_node_encoding(argv[3]) : NULL;
  size_t decoded = 0;
  unsigned char *bytes = xt_node_decode(string->data, string->length, encoding, &decoded);
  uint32_t available = xt_buffer_length(thisValue);
  uint32_t written = 0;
  for (size_t i = 0; i < decoded && offset + written < available; i++) {
    xt_buffer_put(thisValue, offset + written, bytes[i]);
    written++;
  }
  free(bytes);
  return xt_number((double)written);
}

static xt_value xt_buffer_method_fill(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  uint32_t length = xt_buffer_length(thisValue);
  uint32_t start = argc > 1 ? (uint32_t)xt_to_number(argv[1]) : 0;
  uint32_t end = argc > 2 ? (uint32_t)xt_to_number(argv[2]) : length;
  if (end > length) end = length;
  unsigned char byte = 0;
  if (XT_IS_STRING(xt_arg_at(argc, argv, 0))) {
    const char *text = xt_string_data(xt_arg_at(argc, argv, 0));
    if (text && text[0]) byte = (unsigned char)text[0];
  } else if (argc > 0) {
    byte = (unsigned char)((int32_t)xt_to_number(argv[0]) & 0xff);
  }
  for (uint32_t i = start; i < end; i++) xt_buffer_put(thisValue, i, byte);
  return thisValue;
}

static xt_value xt_buffer_method_reverse(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  uint32_t length = xt_buffer_length(thisValue);
  for (uint32_t i = 0; i < length / 2; i++) {
    unsigned char a = xt_buffer_byte(thisValue, i);
    xt_buffer_put(thisValue, i, xt_buffer_byte(thisValue, length - 1 - i));
    xt_buffer_put(thisValue, length - 1 - i, a);
  }
  return thisValue;
}
