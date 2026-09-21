/* -- number methods ------------------------------------------------------- */

xt_value xt_ext_number_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  double n = xt_to_double(target);
  if (strcmp(method, "toFixed") == 0) {
    int precision = argc >= 1 ? xt_to_int32(argv[0]) : 0;
    if (precision < 0) precision = 0;
    if (precision > 100) precision = 100;
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%.*f", precision, n);
    return xt_string_from_cstr(buffer);
  }
  if (strcmp(method, "toPrecision") == 0) {
    if (argc == 0 || argv[0] == XT_UNDEFINED) return xt_to_string(target);
    int precision = xt_to_int32(argv[0]);
    if (precision < 1) precision = 1;
    if (precision > 100) precision = 100;
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%.*g", precision, n);
    return xt_string_from_cstr(buffer);
  }
  if (strcmp(method, "toExponential") == 0) {
    int precision = argc >= 1 ? xt_to_int32(argv[0]) : 6;
    if (precision < 0) precision = 0;
    if (precision > 100) precision = 100;
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%.*e", precision, n);
    return xt_string_from_cstr(buffer);
  }
  if (strcmp(method, "toString") == 0) {
    int radix = argc >= 1 ? xt_to_int32(argv[0]) : 10;
    return xt_number_to_radix(n, radix);
  }
  if (strcmp(method, "valueOf") == 0) return target;
  *handled = 0;
  return XT_UNDEFINED;
}
