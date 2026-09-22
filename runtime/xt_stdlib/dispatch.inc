/* -- method dispatch ------------------------------------------------------ */

static xt_value xt_array_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "push") == 0) {
    xt_value result = xt_number(0);
    for (int32_t i = 0; i < argc; i++) result = xt_array_push(target, argv[i]);
    if (argc == 0) result = xt_number((double)xt_as_array(target)->length);
    return result;
  }
  if (strcmp(method, "pop") == 0) return xt_array_pop(target);
  if (strcmp(method, "shift") == 0) return xt_array_shift(target);
  if (strcmp(method, "unshift") == 0) return xt_array_unshift(target, argc, argv);
  if (strcmp(method, "join") == 0) return xt_array_join(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "slice") == 0) return xt_array_slice(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "indexOf") == 0) return xt_array_index_of(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), 0);
  if (strcmp(method, "includes") == 0) return xt_bool(xt_truthy(xt_ne(xt_array_index_of(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), 1), xt_number(-1))));
  if (strcmp(method, "concat") == 0) return xt_array_concat(target, argc, argv);
  if (strcmp(method, "forEach") == 0) return xt_array_for_each(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "map") == 0) return xt_array_map(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "filter") == 0) return xt_array_filter(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "reduce") == 0) {
    int32_t hasInitial = argc >= 2 ? 1 : 0;
    return xt_array_reduce(target, xt_arg_at(argc, argv, 0), hasInitial, xt_arg_at(argc, argv, 1));
  }
  if (strcmp(method, "reverse") == 0) {
    xt_array *array = xt_as_array(target);
    for (uint32_t i = 0; i + 1 < array->length; i++, array->length--) {
      xt_value tmp = array->items[i];
      array->items[i] = array->items[array->length - 1];
      array->items[array->length - 1] = tmp;
    }
    return target;
  }
  *handled = 0;
  return XT_UNDEFINED;
}

static xt_value xt_string_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "charAt") == 0) return xt_string_char_at(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "charCodeAt") == 0) return xt_string_char_code_at(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "indexOf") == 0) return xt_string_index_of(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "includes") == 0) return xt_bool(xt_truthy(xt_ne(xt_string_index_of(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1)), xt_number(-1))));
  if (strcmp(method, "slice") == 0) return xt_string_slice(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "substring") == 0) return xt_string_substring(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "substr") == 0) return xt_string_substr(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "split") == 0) return xt_string_split(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "toUpperCase") == 0) return xt_string_case(target, 1);
  if (strcmp(method, "toLowerCase") == 0) return xt_string_case(target, 0);
  if (strcmp(method, "trim") == 0) return xt_string_trim(target);
  if (strcmp(method, "replace") == 0) return xt_string_replace(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "repeat") == 0) return xt_string_repeat(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "startsWith") == 0) return xt_string_starts_with(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "endsWith") == 0) return xt_string_ends_with(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "concat") == 0) return xt_string_concat_args(target, argc, argv);
  *handled = 0;
  return XT_UNDEFINED;
}

xt_value xt_call_method(xt_value target, xt_value name, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(name);
  if (!method) return XT_UNDEFINED;
  if (target == XT_UNDEFINED || target == XT_NULL) {
    const char *base = target == XT_NULL ? "null" : "undefined";
    char message[256];
    snprintf(message, sizeof(message),
             "TypeError: Cannot read properties of %s (reading '%s')", base, method);
    xt_throw(xt_string_from_cstr(message));
  }
  int handled = 0;
  if (XT_IS_OBJECT(target)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(target);
    int kind = obj->header.kind;
    if (kind == XT_OBJECT_KIND_PROMISE) {
      xt_value result = xt_promise_method(target, method, argc, argv, &handled);
      if (handled) return result;
    }
    if (kind != XT_OBJECT_KIND_OBJECT && kind != XT_OBJECT_KIND_FUNCTION) {
      xt_value result = xt_ext_container_method(target, method, argc, argv, &handled);
      if (handled) return result;
    }
    xt_value fn = xt_object_get(target, name);
    if (XT_IS_FUNCTION(fn)) return xt_call_with_this(fn, target, argc, argv);
    xt_value result = xt_ext_object_method(target, method, argc, argv, &handled);
    if (handled) return result;
    {
      char message[256];
      snprintf(message, sizeof(message), "TypeError: object has no callable method '%s'", method);
      xt_throw(xt_string_from_cstr(message));
    }
    return XT_UNDEFINED;
  }
  if (XT_IS_FUNCTION(target)) {
    xt_value fn = xt_object_get(target, name);
    if (XT_IS_FUNCTION(fn)) return xt_call_with_this(fn, target, argc, argv);
    {
      char message[256];
      snprintf(message, sizeof(message), "TypeError: function has no callable method '%s'", method);
      xt_throw(xt_string_from_cstr(message));
    }
    return XT_UNDEFINED;
  }
  if (XT_IS_ARRAY(target)) {
    xt_value result = xt_ext_array_method(target, method, argc, argv, &handled);
    if (handled) return result;
    return xt_array_method(target, method, argc, argv, &handled);
  }
  if (XT_IS_STRING(target)) {
    xt_value result = xt_ext_string_method(target, method, argc, argv, &handled);
    if (handled) return result;
    return xt_string_method(target, method, argc, argv, &handled);
  }
  if (XT_IS_BIGINT(target)) {
    xt_value result = xt_bigint_method(target, method, argc, argv, &handled);
    if (handled) return result;
  }
  if (XT_IS_NUMBER(target)) {
    xt_value result = xt_ext_number_method(target, method, argc, argv, &handled);
    if (handled) return result;
  }
  return XT_UNDEFINED;
}
