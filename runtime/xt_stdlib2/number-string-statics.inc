/* -- Number / String statics ---------------------------------------------- */

xt_value xt_number_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "isInteger") == 0) {
    xt_value v = xt_arg_at(argc, argv, 0);
    if (!XT_IS_NUMBER(v)) return XT_FALSE;
    double d = xt_to_double(v);
    return xt_bool(isfinite(d) && floor(d) == d);
  }
  if (strcmp(fn, "isSafeInteger") == 0) {
    xt_value v = xt_arg_at(argc, argv, 0);
    if (!XT_IS_NUMBER(v)) return XT_FALSE;
    double d = xt_to_double(v);
    return xt_bool(isfinite(d) && floor(d) == d && fabs(d) <= 9007199254740991.0);
  }
  if (strcmp(fn, "isFinite") == 0) {
    xt_value v = xt_arg_at(argc, argv, 0);
    if (!XT_IS_NUMBER(v)) return XT_FALSE;
    return xt_bool(isfinite(xt_to_double(v)));
  }
  if (strcmp(fn, "isNaN") == 0) {
    xt_value v = xt_arg_at(argc, argv, 0);
    if (!XT_IS_NUMBER(v)) return XT_FALSE;
    return xt_bool(isnan(xt_to_double(v)));
  }
  if (strcmp(fn, "parseFloat") == 0) return xt_parse_float(argc, argv);
  if (strcmp(fn, "parseInt") == 0) return xt_parse_int(argc, argv);
  if (strcmp(fn, "MAX_SAFE_INTEGER") == 0) return xt_number(9007199254740991.0);
  if (strcmp(fn, "MIN_SAFE_INTEGER") == 0) return xt_number(-9007199254740991.0);
  if (strcmp(fn, "MAX_VALUE") == 0) return xt_number(1.7976931348623157e308);
  if (strcmp(fn, "MIN_VALUE") == 0) return xt_number(5e-324);
  if (strcmp(fn, "EPSILON") == 0) return xt_number(2.220446049250313e-16);
  if (strcmp(fn, "POSITIVE_INFINITY") == 0) return xt_number(INFINITY);
  if (strcmp(fn, "NEGATIVE_INFINITY") == 0) return xt_number(-INFINITY);
  if (strcmp(fn, "NaN") == 0) return xt_number(NAN);
  return XT_UNDEFINED;
}

xt_value xt_string_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "fromCharCode") == 0 || strcmp(fn, "fromCodePoint") == 0) {
    xt_string *out = xt_string_alloc((size_t)argc * 4 + 1);
    size_t length = 0;
    for (int32_t i = 0; i < argc; i++) {
      unsigned code = (unsigned)xt_to_int32(argv[i]);
      char buffer[4];
      int bytes = 0;
      if (code < 0x80) buffer[bytes++] = (char)code;
      else if (code < 0x800) { buffer[bytes++] = (char)(0xC0 | (code >> 6)); buffer[bytes++] = (char)(0x80 | (code & 0x3F)); }
      else if (code < 0x10000) { buffer[bytes++] = (char)(0xE0 | (code >> 12)); buffer[bytes++] = (char)(0x80 | ((code >> 6) & 0x3F)); buffer[bytes++] = (char)(0x80 | (code & 0x3F)); }
      else { buffer[bytes++] = (char)(0xF0 | (code >> 18)); buffer[bytes++] = (char)(0x80 | ((code >> 12) & 0x3F)); buffer[bytes++] = (char)(0x80 | ((code >> 6) & 0x3F)); buffer[bytes++] = (char)(0x80 | (code & 0x3F)); }
      memcpy(out->data + length, buffer, (size_t)bytes);
      length += (size_t)bytes;
    }
    out->length = (uint32_t)length;
    return XT_FROM_PTR(XT_TAG_STRING, out);
  }
  if (strcmp(fn, "raw") == 0) {
    xt_value strings = xt_arg_at(argc, argv, 0);
    xt_value raw = xt_get(strings, xt_string_from_cstr("raw"));
    xt_value out = xt_string_from_cstr("");
    double length = xt_to_number(xt_get(raw, xt_string_from_cstr("length")));
    for (int32_t i = 0; i < (int32_t)length; i++) {
      out = xt_add(out, xt_get(raw, xt_number((double)i)));
      if (i + 1 < (int32_t)length && i + 1 < argc) out = xt_add(out, xt_to_string(argv[i + 1]));
    }
    return out;
  }
  return XT_UNDEFINED;
}
