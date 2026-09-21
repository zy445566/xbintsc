/* -- static API ----------------------------------------------------------- */

xt_value xt_buffer_ctor(int32_t argc, xt_value *argv) {
  if (argc == 0) return xt_buffer_new(0, NULL);
  if (XT_IS_NUMBER(argv[0])) {
    return xt_buffer_alloc((uint32_t)xt_to_number(argv[0]), XT_UNDEFINED, NULL);
  }
  return xt_buffer_from_args(argc, argv);
}

xt_value xt_buffer_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return xt_undefined();
  if (strcmp(fn, "from") == 0 || strcmp(fn, "of") == 0) return xt_buffer_from_args(argc, argv);
  if (strcmp(fn, "alloc") == 0 || strcmp(fn, "allocUnsafe") == 0 || strcmp(fn, "allocUnsafeSlow") == 0) {
    uint32_t size = argc > 0 ? (uint32_t)xt_to_number(argv[0]) : 0;
    const char *encoding = argc > 2 ? xt_node_encoding(argv[2]) : NULL;
    return xt_buffer_alloc(size, argc > 1 ? argv[1] : XT_UNDEFINED, encoding);
  }
  if (strcmp(fn, "isBuffer") == 0) return xt_bool(xt_buffer_is(xt_arg_at(argc, argv, 0)));
  if (strcmp(fn, "byteLength") == 0) {
    xt_value value = xt_arg_at(argc, argv, 0);
    if (xt_buffer_is(value)) return xt_number((double)xt_buffer_length(value));
    const char *encoding = argc > 1 ? xt_node_encoding(argv[1]) : NULL;
    xt_value text = xt_to_string(value);
    xt_string *string = xt_as_string(text);
    if (encoding && strcmp(encoding, "hex") == 0) return xt_number((double)(string->length / 2));
    if (encoding && strcmp(encoding, "base64") == 0) return xt_number((double)(string->length / 4 * 3));
    return xt_number((double)string->length);
  }
  if (strcmp(fn, "concat") == 0) {
    xt_value list = xt_arg_at(argc, argv, 0);
    uint32_t total = 0;
    uint32_t count = XT_IS_ARRAY(list) ? (uint32_t)xt_to_number(xt_array_length(list)) : 0;
    for (uint32_t i = 0; i < count; i++) total += xt_buffer_length(xt_array_get(list, xt_number((double)i)));
    if (argc > 1 && XT_IS_NUMBER(argv[1])) total = (uint32_t)xt_to_number(argv[1]);
    xt_value result = xt_buffer_new(total, NULL);
    uint32_t offset = 0;
    for (uint32_t i = 0; i < count && offset < total; i++) {
      xt_value item = xt_array_get(list, xt_number((double)i));
      uint32_t length = xt_buffer_length(item);
      for (uint32_t j = 0; j < length && offset < total; j++) xt_buffer_put(result, offset++, xt_buffer_byte(item, j));
    }
    return result;
  }
  if (strcmp(fn, "compare") == 0) {
    xt_value a = xt_arg_at(argc, argv, 0);
    xt_value b = xt_arg_at(argc, argv, 1);
    uint32_t la = xt_buffer_length(a), lb = xt_buffer_length(b);
    uint32_t n = la < lb ? la : lb;
    for (uint32_t i = 0; i < n; i++) {
      unsigned char x = xt_buffer_byte(a, i), y = xt_buffer_byte(b, i);
      if (x != y) return xt_number(x < y ? -1 : 1);
    }
    return xt_number(la < lb ? -1 : la > lb ? 1 : 0);
  }
  return xt_undefined();
}
