/* ------------------------------------------------------------------------- */
/* Standard library: Array / String methods                                  */
/* ------------------------------------------------------------------------- */

xt_value xt_arg_at(int32_t argc, xt_value *argv, int32_t index) {
  if (!argv || index < 0 || index >= argc) return XT_UNDEFINED;
  return argv[index];
}

static int xt_same_value_zero(xt_value a, xt_value b) {
  if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) {
    double x = xt_to_double(a);
    double y = xt_to_double(b);
    if (isnan(x) && isnan(y)) return 1;
    return x == y;
  }
  return xt_truthy(xt_seq(a, b));
}

xt_array *xt_as_array(xt_value value) {
  if (XT_IS_ARRAY(value)) return (xt_array *)XT_GET_PTR(value);
  return NULL;
}
