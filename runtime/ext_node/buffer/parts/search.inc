/* -- search --------------------------------------------------------------- */

static const unsigned char *xt_buffer_search_needle(xt_value value, const char *encoding, size_t *length,
                                                     unsigned char *single) {
  if (XT_IS_NUMBER(value)) {
    *single = (unsigned char)((int32_t)xt_to_number(value) & 0xff);
    *length = 1;
    return single;
  }
  if (xt_buffer_is(value)) {
    uint32_t n = xt_buffer_length(value);
    unsigned char *bytes = (unsigned char *)malloc(n ? n : 1);
    for (uint32_t i = 0; i < n; i++) bytes[i] = xt_buffer_byte(value, i);
    *length = n;
    return bytes;
  }
  xt_value text = xt_to_string(value);
  xt_string *string = xt_as_string(text);
  size_t decoded = 0;
  unsigned char *bytes = xt_node_decode(string->data, string->length, encoding, &decoded);
  *length = decoded;
  return bytes;
}

static xt_value xt_buffer_method_index_of(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv,
                                           int reverse) {
  (void)env;
  const char *encoding = argc > 2 ? xt_node_encoding(argv[2]) : NULL;
  size_t needleLength = 0;
  unsigned char single = 0;
  const unsigned char *needle = xt_buffer_search_needle(xt_arg_at(argc, argv, 0), encoding, &needleLength, &single);
  uint32_t length = xt_buffer_length(thisValue);
  int32_t start = argc > 1 ? (int32_t)xt_to_number(argv[1]) : (reverse ? (int32_t)length - 1 : 0);
  if (start < 0) start += (int32_t)length;
  if (start < 0) start = 0;
  if (needleLength == 0 || needleLength > length) {
    if (!xt_buffer_is(xt_arg_at(argc, argv, 0))) free((void *)needle);
    return xt_number(-1);
  }
  int found = -1;
  if (reverse) {
    if (start > (int32_t)length - 1) start = (int32_t)length - 1;
    for (int32_t i = start; i >= 0; i--) {
      int match = 1;
      for (size_t j = 0; j < needleLength; j++) {
        if ((uint32_t)(i + (int32_t)j) >= length || xt_buffer_byte(thisValue, (uint32_t)(i + (int32_t)j)) != needle[j]) {
          match = 0;
          break;
        }
      }
      if (match) {
        found = i;
        break;
      }
    }
  } else {
    for (uint32_t i = (uint32_t)start; i + needleLength <= length; i++) {
      int match = 1;
      for (size_t j = 0; j < needleLength; j++) {
        if (xt_buffer_byte(thisValue, i + (uint32_t)j) != needle[j]) {
          match = 0;
          break;
        }
      }
      if (match) {
        found = (int)i;
        break;
      }
    }
  }
  if (!xt_buffer_is(xt_arg_at(argc, argv, 0))) free((void *)needle);
  return xt_number((double)found);
}

static xt_value xt_buffer_method_index_of_fwd(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  return xt_buffer_method_index_of(thisValue, env, argc, argv, 0);
}

static xt_value xt_buffer_method_index_of_rev(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  return xt_buffer_method_index_of(thisValue, env, argc, argv, 1);
}

static xt_value xt_buffer_method_includes(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  xt_value result = xt_buffer_method_index_of(thisValue, env, argc, argv, 0);
  return xt_bool(xt_to_number(result) >= 0);
}
