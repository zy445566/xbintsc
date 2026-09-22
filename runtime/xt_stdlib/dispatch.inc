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
    for (uint32_t i = 0; i + 1 < array->length - i; i++) {
      xt_value tmp = array->items[i];
      array->items[i] = array->items[array->length - 1 - i];
      array->items[array->length - 1 - i] = tmp;
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
  if (strcmp(method, "split") == 0) return xt_string_split(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
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

/* Backing trampoline for `fn.bind(thisArg, ...partial)`: the bound target,
 * `this` value and partial argument array travel in the closure environment. */
static xt_value xt_bound_invoke(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value target = xt_closure_env(env, 0);
  xt_value boundThis = xt_closure_env(env, 1);
  xt_value boundArgs = xt_closure_env(env, 2);
  int32_t boundCount = XT_IS_ARRAY(boundArgs) ? (int32_t)xt_as_array(boundArgs)->length : 0;
  int32_t total = boundCount + argc;
  xt_value *combined = total > 0 ? (xt_value *)malloc(sizeof(xt_value) * (size_t)total) : NULL;
  if (combined) {
    for (int32_t i = 0; i < boundCount; i++) combined[i] = xt_as_array(boundArgs)->items[i];
    for (int32_t i = 0; i < argc; i++) combined[boundCount + i] = argv[i];
  }
  xt_value result = xt_call_with_this(target, boundThis, total, combined);
  free(combined);
  return result;
}

/* Backing trampoline for first-class built-in method values (`arr.map`,
 * `"".slice`, `(1).toFixed`). The value is *unbound*, exactly like JavaScript:
 * the receiver arrives through `this`, so a detached call
 * (`const f = arr.map; f(cb)`) throws, while `arr.map(cb)` and
 * `arr.map?.(cb)` (which pass the receiver) work. The method name lives in
 * environment slot 0. */
static xt_value xt_method_value_invoke(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  xt_value name = xt_closure_env(env, 0);
  if (thisValue == XT_UNDEFINED || thisValue == XT_NULL) {
    const char *method = xt_string_data(name);
    char message[256];
    snprintf(message, sizeof(message),
             "TypeError: Cannot read properties of undefined (reading '%s')",
             method ? method : "method");
    xt_throw(xt_string_from_cstr(message));
  }
  return xt_call_method(thisValue, name, argc, argv);
}

#define XT_NAME_COUNT(table) (sizeof(table) / sizeof((table)[0]))

typedef struct {
  const char *name;
  int32_t arity;
} xt_method_spec;

static const xt_method_spec XT_ARRAY_METHODS[] = {
  {"push", 1}, {"pop", 0}, {"shift", 0}, {"unshift", 1}, {"join", 1}, {"slice", 2},
  {"indexOf", 1}, {"lastIndexOf", 1}, {"includes", 1}, {"concat", 1}, {"forEach", 1},
  {"map", 1}, {"filter", 1}, {"reduce", 1}, {"reduceRight", 1}, {"reverse", 0},
  {"at", 1}, {"find", 1}, {"findIndex", 1}, {"findLast", 1}, {"findLastIndex", 1},
  {"some", 1}, {"every", 1}, {"sort", 1}, {"flat", 0}, {"flatMap", 1}, {"fill", 1},
  {"copyWithin", 2}, {"splice", 2}, {"toReversed", 0}, {"toSorted", 1}, {"toSpliced", 2},
  {"with", 2}, {"keys", 0}, {"values", 0}, {"entries", 0}, {"toString", 0},
  {"toLocaleString", 0},
};

static const xt_method_spec XT_STRING_METHODS[] = {
  {"charAt", 1}, {"charCodeAt", 1}, {"codePointAt", 1}, {"indexOf", 1}, {"lastIndexOf", 1},
  {"includes", 1}, {"slice", 2}, {"substring", 2}, {"substr", 2}, {"split", 2},
  {"toUpperCase", 0}, {"toLowerCase", 0}, {"trim", 0}, {"trimStart", 0}, {"trimEnd", 0},
  {"replace", 2}, {"replaceAll", 2}, {"repeat", 1}, {"startsWith", 1}, {"endsWith", 1},
  {"concat", 1}, {"at", 1}, {"padStart", 1}, {"padEnd", 1}, {"match", 1}, {"search", 1},
  {"localeCompare", 1}, {"toString", 0}, {"valueOf", 0},
};

static const xt_method_spec XT_NUMBER_METHODS[] = {
  {"toFixed", 1}, {"toPrecision", 1}, {"toExponential", 1}, {"toString", 1},
  {"valueOf", 0}, {"toLocaleString", 0},
};

static const xt_method_spec XT_FUNCTION_METHODS[] = {
  {"call", 1}, {"apply", 2}, {"bind", 1}, {"toString", 0},
};

static const xt_method_spec XT_BIGINT_METHODS[] = {
  {"toString", 0}, {"toLocaleString", 0}, {"valueOf", 0},
};

static const xt_method_spec XT_BOOLEAN_METHODS[] = {
  {"toString", 0}, {"valueOf", 0},
};

static const xt_method_spec XT_DATE_METHODS[] = {
  {"getTime", 0}, {"getFullYear", 0}, {"getMonth", 0}, {"getDate", 0}, {"getDay", 0},
  {"getHours", 0}, {"getMinutes", 0}, {"getSeconds", 0}, {"getMilliseconds", 0},
  {"getUTCFullYear", 0}, {"getUTCMonth", 0}, {"getUTCDate", 0}, {"getUTCDay", 0},
  {"getUTCHours", 0}, {"getUTCMinutes", 0}, {"getUTCSeconds", 0}, {"getUTCMilliseconds", 0},
  {"toISOString", 0}, {"toJSON", 1}, {"toString", 0}, {"valueOf", 0},
};

static const xt_method_spec XT_MAP_METHODS[] = {
  {"get", 1}, {"set", 2}, {"has", 1}, {"delete", 1}, {"clear", 0}, {"keys", 0},
  {"values", 0}, {"entries", 0}, {"forEach", 1},
};

static const xt_method_spec XT_SET_METHODS[] = {
  {"add", 1}, {"has", 1}, {"delete", 1}, {"clear", 0}, {"keys", 0}, {"values", 0},
  {"entries", 0}, {"forEach", 1},
};

static const xt_method_spec XT_REGEXP_METHODS[] = {
  {"test", 1}, {"exec", 1}, {"toString", 0},
};

static const xt_method_spec XT_PROMISE_METHODS[] = {
  {"then", 2}, {"catch", 1}, {"finally", 1},
};

static const xt_method_spec XT_ERROR_METHODS[] = {
  {"toString", 0},
};

static const xt_method_spec XT_GENERATOR_METHODS[] = {
  {"next", 1}, {"return", 1}, {"throw", 1},
};

static const xt_method_spec XT_SYMBOL_METHODS[] = {
  {"toString", 0}, {"valueOf", 0},
};

static int xt_method_lookup(const xt_method_spec *specs, size_t count, const char *method, int32_t *arity) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(method, specs[i].name) == 0) {
      if (arity) *arity = specs[i].arity;
      return 1;
    }
  }
  return 0;
}

xt_value xt_method_value(xt_value target, xt_value name) {
  if (!XT_IS_STRING(name)) return XT_UNDEFINED;
  const char *method = xt_string_data(name);
  if (!method) return XT_UNDEFINED;
  int32_t arity = 0;
  int known = 0;
  if (XT_IS_ARRAY(target)) {
    known = xt_method_lookup(XT_ARRAY_METHODS, XT_NAME_COUNT(XT_ARRAY_METHODS), method, &arity);
  } else if (XT_IS_STRING(target)) {
    known = xt_method_lookup(XT_STRING_METHODS, XT_NAME_COUNT(XT_STRING_METHODS), method, &arity);
  } else if (XT_IS_FUNCTION(target)) {
    known = xt_method_lookup(XT_FUNCTION_METHODS, XT_NAME_COUNT(XT_FUNCTION_METHODS), method, &arity);
  } else if (XT_IS_BIGINT(target)) {
    known = xt_method_lookup(XT_BIGINT_METHODS, XT_NAME_COUNT(XT_BIGINT_METHODS), method, &arity);
  } else if (XT_IS_NUMBER(target)) {
    known = xt_method_lookup(XT_NUMBER_METHODS, XT_NAME_COUNT(XT_NUMBER_METHODS), method, &arity);
  } else if (target == XT_TRUE || target == XT_FALSE) {
    known = xt_method_lookup(XT_BOOLEAN_METHODS, XT_NAME_COUNT(XT_BOOLEAN_METHODS), method, &arity);
  } else if (XT_IS_OBJECT(target)) {
    int kind = ((xt_object *)XT_GET_PTR(target))->header.kind;
    if (kind == XT_OBJECT_KIND_DATE) {
      known = xt_method_lookup(XT_DATE_METHODS, XT_NAME_COUNT(XT_DATE_METHODS), method, &arity);
    } else if (kind == XT_OBJECT_KIND_MAP) {
      known = xt_method_lookup(XT_MAP_METHODS, XT_NAME_COUNT(XT_MAP_METHODS), method, &arity);
    } else if (kind == XT_OBJECT_KIND_SET) {
      known = xt_method_lookup(XT_SET_METHODS, XT_NAME_COUNT(XT_SET_METHODS), method, &arity);
    } else if (kind == XT_OBJECT_KIND_REGEXP) {
      known = xt_method_lookup(XT_REGEXP_METHODS, XT_NAME_COUNT(XT_REGEXP_METHODS), method, &arity);
    } else if (kind == XT_OBJECT_KIND_PROMISE) {
      known = xt_method_lookup(XT_PROMISE_METHODS, XT_NAME_COUNT(XT_PROMISE_METHODS), method, &arity);
    } else if (kind == XT_OBJECT_KIND_ERROR) {
      known = xt_method_lookup(XT_ERROR_METHODS, XT_NAME_COUNT(XT_ERROR_METHODS), method, &arity);
    } else if (kind == XT_OBJECT_KIND_GENERATOR) {
      known = xt_method_lookup(XT_GENERATOR_METHODS, XT_NAME_COUNT(XT_GENERATOR_METHODS), method, &arity);
    } else if (kind == XT_OBJECT_KIND_SYMBOL) {
      known = xt_method_lookup(XT_SYMBOL_METHODS, XT_NAME_COUNT(XT_SYMBOL_METHODS), method, &arity);
    }
  }
  if (!known) return XT_UNDEFINED;
  xt_value env[1];
  env[0] = name;
  xt_value closure = xt_closure_new((void *)xt_method_value_invoke, 1, env);
  xt_function_set_metadata(closure, name, arity);
  return closure;
}

xt_value xt_call_method(xt_value target, xt_value name, int32_t argc, xt_value *argv) {
  /* A symbol-named method (`obj[Symbol.iterator]()`) is never a built-in, so
   * resolve it as an ordinary property and call it with `target` as `this`. */
  if (xt_is_symbol(name)) {
    if (target == XT_UNDEFINED || target == XT_NULL) {
      xt_throw(xt_string_from_cstr("TypeError: Cannot read properties of null (reading a Symbol)"));
    }
    xt_value fn = xt_get(target, name);
    if (XT_IS_FUNCTION(fn)) return xt_call_with_this(fn, target, argc, argv);
    return XT_UNDEFINED;
  }
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
  if (xt_is_symbol(target)) {
    if (strcmp(method, "toString") == 0) return xt_symbol_to_string(target);
    if (strcmp(method, "valueOf") == 0) return target;
    return XT_UNDEFINED;
  }
  if (XT_IS_OBJECT(target)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(target);
    int kind = obj->header.kind;
    if (kind == XT_OBJECT_KIND_PROMISE) {
      xt_value result = xt_promise_method(target, method, argc, argv, &handled);
      if (handled) return result;
    }
    if (kind == XT_OBJECT_KIND_ERROR && strcmp(method, "toString") == 0) {
      return xt_error_to_string(target);
    }
    if (kind == XT_OBJECT_KIND_GENERATOR) {
      if (strcmp(method, "next") == 0) return xt_generator_next(target, xt_arg_at(argc, argv, 0));
      if (strcmp(method, "throw") == 0) return xt_generator_throw(target, xt_arg_at(argc, argv, 0));
      if (strcmp(method, "return") == 0) return xt_generator_return(target, xt_arg_at(argc, argv, 0));
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
    /* Function prototype methods, resolved before the property bag. */
    if (strcmp(method, "call") == 0) {
      return xt_call_with_this(target, xt_arg_at(argc, argv, 0),
                               argc > 0 ? argc - 1 : 0, argc > 0 ? argv + 1 : NULL);
    }
    if (strcmp(method, "apply") == 0) {
      xt_value thisArg = xt_arg_at(argc, argv, 0);
      xt_value args = xt_arg_at(argc, argv, 1);
      if (XT_IS_ARRAY(args)) {
        xt_array *array = xt_as_array(args);
        return xt_call_with_this(target, thisArg, (int32_t)array->length, array->items);
      }
      return xt_call_with_this(target, thisArg, 0, NULL);
    }
    if (strcmp(method, "bind") == 0) {
      xt_value env[3];
      env[0] = target;
      env[1] = xt_arg_at(argc, argv, 0);
      env[2] = argc > 1 ? xt_array_new(argc - 1, argv + 1) : xt_array_new(0, NULL);
      xt_value bound = xt_closure_new((void *)xt_bound_invoke, 3, env);
      /* Bound functions report `name` as "bound <target>" and reduce `length`
       * by the number of pre-bound arguments, like the specification. */
      xt_function *targetFn = (xt_function *)XT_GET_PTR(target);
      const char *base = targetFn->name ? targetFn->name->data : "";
      char boundName[256];
      snprintf(boundName, sizeof(boundName), "bound %s", base);
      int32_t prebound = argc > 1 ? argc - 1 : 0;
      int32_t arity = targetFn->arity >= 0 ? targetFn->arity - prebound : 0;
      if (arity < 0) arity = 0;
      xt_function_set_metadata(bound, xt_string_from_cstr(boundName), arity);
      return bound;
    }
    xt_value fn = xt_object_get(target, name);
    if (XT_IS_FUNCTION(fn)) return xt_call_with_this(fn, target, argc, argv);
    if (strcmp(method, "toString") == 0) {
      /* Source text is not retained; report a native-style representation. */
      xt_function *self = (xt_function *)XT_GET_PTR(target);
      const char *selfName = self->name ? self->name->data : "";
      char buffer[256];
      snprintf(buffer, sizeof(buffer), "function %s() { [native code] }", selfName);
      return xt_string_from_cstr(buffer);
    }
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
  if (target == XT_TRUE || target == XT_FALSE) {
    if (strcmp(method, "toString") == 0) return target == XT_TRUE ? xt_string_from_cstr("true") : xt_string_from_cstr("false");
    if (strcmp(method, "valueOf") == 0) return target;
    return XT_UNDEFINED;
  }
  return XT_UNDEFINED;
}
