/* -- Array statics -------------------------------------------------------- */

xt_value xt_array_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "isArray") == 0) return xt_bool(XT_IS_ARRAY(xt_arg_at(argc, argv, 0)));
  if (strcmp(fn, "of") == 0) return xt_array_new(argc, argv);
  if (strcmp(fn, "from") == 0) {
    xt_value source = xt_arg_at(argc, argv, 0);
    xt_value mapper = xt_arg_at(argc, argv, 1);
    xt_value out = xt_array_new(0, NULL);
    if (XT_IS_ARRAY(source)) {
      xt_array *array = xt_as_array(source);
      for (uint32_t i = 0; i < array->length; i++) {
        xt_value item = array->items[i];
        if (XT_IS_FUNCTION(mapper)) { xt_value args[2] = {item, xt_number((double)i)}; item = xt_closure_call(mapper, 2, args); }
        xt_array_push(out, item);
      }
    } else if (XT_IS_STRING(source)) {
      xt_string *s = xt_as_string(source);
      for (uint32_t i = 0; i < s->length; i++) {
        xt_value item = xt_string_new(s->data + i, 1);
        if (XT_IS_FUNCTION(mapper)) { xt_value args[2] = {item, xt_number((double)i)}; item = xt_closure_call(mapper, 2, args); }
        xt_array_push(out, item);
      }
    } else {
      double length = xt_to_number(xt_get(source, xt_string_from_cstr("length")));
      for (int32_t i = 0; i < (int32_t)length; i++) {
        xt_value item = xt_get(source, xt_number((double)i));
        if (XT_IS_FUNCTION(mapper)) { xt_value args[2] = {item, xt_number((double)i)}; item = xt_closure_call(mapper, 2, args); }
        xt_array_push(out, item);
      }
    }
    return out;
  }
  return XT_UNDEFINED;
}
