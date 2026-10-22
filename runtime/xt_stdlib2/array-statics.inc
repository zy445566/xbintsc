/* -- Array statics -------------------------------------------------------- */

/* True when `Array.from` should consume the value through the iteration
 * protocol instead of the array-like `length` path. Typed arrays stay on the
 * length path because their indexed properties already work that way. */
static int xt_array_from_is_iterable(xt_value value) {
  if (xt_is_map(value) || xt_is_set(value) || xt_is_generator(value)) return 1;
  if (XT_IS_OBJECT(value) && xt_typed_array_kind_of(value) < 0) {
    return XT_IS_FUNCTION(xt_object_get(value, xt_symbol_well_known("iterator")));
  }
  return 0;
}

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
      uint32_t points = xt_string_code_point_count(s);
      for (uint32_t i = 0; i < points; i++) {
        xt_value item = xt_string_code_point_value(s, (int32_t)i);
        if (XT_IS_FUNCTION(mapper)) { xt_value args[2] = {item, xt_number((double)i)}; item = xt_closure_call(mapper, 2, args); }
        xt_array_push(out, item);
      }
    } else if (xt_array_from_is_iterable(source)) {
      /* Map, Set, generators, iterator objects and custom `[Symbol.iterator]`
       * values do not expose a `length`; walk their `next()` protocol instead
       * of silently producing an empty array. */
      xt_value iterator = xt_iter_open(source);
      for (uint32_t i = 0;; i++) {
        if (!xt_truthy(xt_iter_has(iterator, xt_number((double)i)))) break;
        xt_value item = xt_iter_value(iterator, xt_number((double)i));
        if (XT_IS_FUNCTION(mapper)) {
          xt_value args[2] = {item, xt_number((double)i)};
          item = xt_closure_call(mapper, 2, args);
        }
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
