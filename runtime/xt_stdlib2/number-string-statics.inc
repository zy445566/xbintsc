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
  if (strcmp(fn, "fromCharCode") == 0) {
    /* `fromCharCode` takes UTF-16 code units (modulo 65536) and joins adjacent
     * high/low surrogates into one code point, like JavaScript. */
    if (argc <= 0) return xt_string_from_cstr("");
    uint16_t *units = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)argc);
    if (!units) abort();
    for (int32_t i = 0; i < argc; i++) units[i] = (uint16_t)(xt_to_int32(argv[i]) & 0xFFFF);
    xt_value result = xt_string_from_units(units, (size_t)argc);
    free(units);
    return result;
  }
  if (strcmp(fn, "fromCodePoint") == 0) {
    char *out = (char *)malloc((size_t)argc * 4 + 1);
    if (!out) abort();
    size_t length = 0;
    for (int32_t i = 0; i < argc; i++) {
      int32_t code = xt_to_int32(argv[i]);
      if (code < 0 || code > 0x10FFFF) {
        free(out);
        xt_throw(xt_error_from_message("RangeError: Invalid code point"));
        return XT_UNDEFINED;
      }
      length += xt_utf8_encode((uint32_t)code, out + length);
    }
    xt_value result = xt_string_new(out, length);
    free(out);
    return result;
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
