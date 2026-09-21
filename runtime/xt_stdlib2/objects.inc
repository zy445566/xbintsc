xt_value xt_ext_object_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "hasOwnProperty") == 0) return xt_object_has_own(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "toString") == 0) return xt_string_from_cstr("[object Object]");
  if (strcmp(method, "valueOf") == 0) return target;
  *handled = 0;
  return XT_UNDEFINED;
}

/* -- Object statics ------------------------------------------------------- */

xt_value xt_object_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "keys") == 0) return xt_object_keys(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "values") == 0) return xt_object_values(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "entries") == 0) return xt_object_entries(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "assign") == 0) return xt_object_assign(argc, argv);
  if (strcmp(fn, "freeze") == 0) return xt_object_freeze(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "isFrozen") == 0) return xt_bool(xt_object_is_frozen(xt_arg_at(argc, argv, 0)));
  if (strcmp(fn, "fromEntries") == 0) return xt_object_from_entries(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "getPrototypeOf") == 0) return xt_object_get_prototype(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "setPrototypeOf") == 0) return xt_object_set_prototype(xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(fn, "hasOwn") == 0) return xt_object_has_own(xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(fn, "is") == 0) {
    xt_value a = xt_arg_at(argc, argv, 0);
    xt_value b = xt_arg_at(argc, argv, 1);
    if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) {
      double x = xt_to_double(a), y = xt_to_double(b);
      if (isnan(x) && isnan(y)) return XT_TRUE;
      if (x == 0 && y == 0) return xt_bool(signbit(x) == signbit(y));
      return xt_bool(x == y);
    }
    return xt_seq(a, b);
  }
  if (strcmp(fn, "create") == 0) {
    xt_value proto = xt_arg_at(argc, argv, 0);
    return xt_object_new_with_proto(proto == XT_NULL ? XT_UNDEFINED : proto);
  }
  return XT_UNDEFINED;
}
