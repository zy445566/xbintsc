/* xbintsc runtime - typed array instance methods.
 * Included by `xt_typed_array.c` as part of a single translation unit. */

/* -- methods -------------------------------------------------------------- */

static xt_value xt_ta_method_fill(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return self;
  int32_t length = (int32_t)xt_ta_length(self);
  int32_t start = xt_ta_relative(argc > 1 ? xt_to_int32(argv[1]) : 0, length);
  int32_t end = xt_ta_relative(argc > 2 && argv[2] != XT_UNDEFINED ? xt_to_int32(argv[2]) : length, length);
  double value = xt_to_number(xt_arg_at(argc, argv, 0));
  for (int32_t i = start; i < end; i++) xt_ta_store(self, kind, (uint32_t)i, value);
  return self;
}

static xt_value xt_ta_method_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return XT_UNDEFINED;
  xt_value source = xt_arg_at(argc, argv, 0);
  uint32_t sourceLength = 0;
  if (!xt_ta_length_of(source, &sourceLength)) return XT_UNDEFINED;
  int32_t offset = argc > 1 ? xt_to_int32(argv[1]) : 0;
  if (offset < 0 || offset + (int32_t)sourceLength > (int32_t)xt_ta_length(self)) {
    xt_throw(xt_string_from_cstr("RangeError: offset is out of bounds"));
    return XT_UNDEFINED;
  }
  for (uint32_t i = 0; i < sourceLength; i++) {
    xt_value item = xt_get(source, xt_number((double)i));
    xt_ta_store(self, kind, (uint32_t)(offset + (int32_t)i), xt_to_number(item));
  }
  return XT_UNDEFINED;
}

static xt_value xt_ta_method_slice(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return XT_UNDEFINED;
  int32_t length = (int32_t)xt_ta_length(self);
  int32_t start = xt_ta_relative(argc > 0 && argv[0] != XT_UNDEFINED ? xt_to_int32(argv[0]) : 0, length);
  int32_t end = xt_ta_relative(argc > 1 && argv[1] != XT_UNDEFINED ? xt_to_int32(argv[1]) : length, length);
  if (end < start) end = start;
  xt_value out = xt_ta_new(kind, (uint32_t)(end - start));
  for (int32_t i = start; i < end; i++) xt_ta_store(out, kind, (uint32_t)(i - start), xt_to_number(xt_ta_element(self, (uint32_t)i)));
  return out;
}

static xt_value xt_ta_method_join(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value separator = argc > 0 && argv[0] != XT_UNDEFINED ? xt_to_string(argv[0]) : xt_string_from_cstr(",");
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value result = xt_string_from_cstr("");
  for (int32_t i = 0; i < length; i++) {
    if (i > 0) result = xt_add(result, separator);
    result = xt_add(result, xt_to_string(xt_ta_element(self, (uint32_t)i)));
  }
  return result;
}

static xt_value xt_ta_method_to_string(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)argc;
  return xt_ta_method_join(self, env, 0, argv);
}

static xt_value xt_ta_method_index_of(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value needle = xt_arg_at(argc, argv, 0);
  int32_t from = argc > 1 && argv[1] != XT_UNDEFINED ? xt_to_int32(argv[1]) : 0;
  if (from < 0) from += length;
  if (from < 0) from = 0;
  for (int32_t i = from; i < length; i++) {
    if (xt_value_equals(xt_ta_element(self, (uint32_t)i), needle)) return xt_number((double)i);
  }
  return xt_number(-1);
}

static xt_value xt_ta_method_last_index_of(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value needle = xt_arg_at(argc, argv, 0);
  int32_t from = argc > 1 && argv[1] != XT_UNDEFINED ? xt_to_int32(argv[1]) : length - 1;
  if (from < 0) from += length;
  if (from >= length) from = length - 1;
  for (int32_t i = from; i >= 0; i--) {
    if (xt_value_equals(xt_ta_element(self, (uint32_t)i), needle)) return xt_number((double)i);
  }
  return xt_number(-1);
}

static xt_value xt_ta_method_includes(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value needle = xt_arg_at(argc, argv, 0);
  int32_t from = argc > 1 && argv[1] != XT_UNDEFINED ? xt_to_int32(argv[1]) : 0;
  if (from < 0) from += length;
  if (from < 0) from = 0;
  for (int32_t i = from; i < length; i++) {
    if (xt_value_equals(xt_ta_element(self, (uint32_t)i), needle)) return XT_TRUE;
  }
  return XT_FALSE;
}

static xt_value xt_ta_method_for_each(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 0; i < length; i++) {
    xt_value args[3] = {xt_ta_element(self, (uint32_t)i), xt_number((double)i), self};
    xt_call_with_this(callback, thisArg, 3, args);
  }
  return XT_UNDEFINED;
}

static xt_value xt_ta_method_map(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return XT_UNDEFINED;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value out = xt_ta_new(kind, (uint32_t)length);
  for (int32_t i = 0; i < length; i++) {
    xt_value args[3] = {xt_ta_element(self, (uint32_t)i), xt_number((double)i), self};
    xt_ta_store(out, kind, (uint32_t)i, xt_to_number(xt_call_with_this(callback, thisArg, 3, args)));
  }
  return out;
}

static xt_value xt_ta_method_filter(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return XT_UNDEFINED;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value out = xt_ta_new(kind, 0);
  for (int32_t i = 0; i < length; i++) {
    xt_value args[3] = {xt_ta_element(self, (uint32_t)i), xt_number((double)i), self};
    if (xt_truthy(xt_call_with_this(callback, thisArg, 3, args))) {
      xt_ta_append(out, kind, xt_ta_element(self, (uint32_t)i));
    }
  }
  return out;
}

static xt_value xt_ta_method_every(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 0; i < length; i++) {
    xt_value args[3] = {xt_ta_element(self, (uint32_t)i), xt_number((double)i), self};
    if (!xt_truthy(xt_call_with_this(callback, thisArg, 3, args))) return XT_FALSE;
  }
  return XT_TRUE;
}

static xt_value xt_ta_method_some(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 0; i < length; i++) {
    xt_value args[3] = {xt_ta_element(self, (uint32_t)i), xt_number((double)i), self};
    if (xt_truthy(xt_call_with_this(callback, thisArg, 3, args))) return XT_TRUE;
  }
  return XT_FALSE;
}

static xt_value xt_ta_method_find(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 0; i < length; i++) {
    xt_value element = xt_ta_element(self, (uint32_t)i);
    xt_value args[3] = {element, xt_number((double)i), self};
    if (xt_truthy(xt_call_with_this(callback, thisArg, 3, args))) return element;
  }
  return XT_UNDEFINED;
}

static xt_value xt_ta_method_find_index(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 0; i < length; i++) {
    xt_value args[3] = {xt_ta_element(self, (uint32_t)i), xt_number((double)i), self};
    if (xt_truthy(xt_call_with_this(callback, thisArg, 3, args))) return xt_number((double)i);
  }
  return xt_number(-1);
}

static xt_value xt_ta_method_reduce(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  int32_t length = (int32_t)xt_ta_length(self);
  int32_t index = 0;
  xt_value accumulator;
  if (argc > 1) {
    accumulator = argv[1];
  } else if (length > 0) {
    accumulator = xt_ta_element(self, 0);
    index = 1;
  } else {
    xt_throw(xt_string_from_cstr("TypeError: Reduce of empty array with no initial value"));
    return XT_UNDEFINED;
  }
  for (; index < length; index++) {
    xt_value args[4] = {accumulator, xt_ta_element(self, (uint32_t)index), xt_number((double)index), self};
    accumulator = xt_call_with_this(callback, XT_UNDEFINED, 4, args);
  }
  return accumulator;
}

static xt_value xt_ta_method_reverse(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return self;
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 0; i < length / 2; i++) {
    double left = xt_to_number(xt_ta_element(self, (uint32_t)i));
    double right = xt_to_number(xt_ta_element(self, (uint32_t)(length - 1 - i)));
    xt_ta_store(self, kind, (uint32_t)i, right);
    xt_ta_store(self, kind, (uint32_t)(length - 1 - i), left);
  }
  return self;
}

static int xt_ta_compare(xt_value comparator, xt_value a, xt_value b) {
  if (XT_IS_FUNCTION(comparator)) {
    xt_value args[2] = {a, b};
    double result = xt_to_number(xt_call_with_this(comparator, XT_UNDEFINED, 2, args));
    return result < 0 ? -1 : result > 0 ? 1 : 0;
  }
  double x = xt_to_number(a);
  double y = xt_to_number(b);
  if (isnan(x)) return isnan(y) ? 0 : 1;
  if (isnan(y)) return -1;
  return x < y ? -1 : x > y ? 1 : 0;
}

static xt_value xt_ta_method_sort(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return self;
  xt_value comparator = xt_arg_at(argc, argv, 0);
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 1; i < length; i++) {
    xt_value key = xt_ta_element(self, (uint32_t)i);
    int32_t j = i - 1;
    while (j >= 0 && xt_ta_compare(comparator, xt_ta_element(self, (uint32_t)j), key) > 0) {
      xt_ta_store(self, kind, (uint32_t)(j + 1), xt_to_number(xt_ta_element(self, (uint32_t)j)));
      j--;
    }
    xt_ta_store(self, kind, (uint32_t)(j + 1), xt_to_number(key));
  }
  return self;
}

static xt_value xt_ta_method_copy_within(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return self;
  int32_t length = (int32_t)xt_ta_length(self);
  int32_t target = xt_ta_relative(xt_to_int32(xt_arg_at(argc, argv, 0)), length);
  int32_t start = xt_ta_relative(argc > 1 && argv[1] != XT_UNDEFINED ? xt_to_int32(argv[1]) : 0, length);
  int32_t end = xt_ta_relative(argc > 2 && argv[2] != XT_UNDEFINED ? xt_to_int32(argv[2]) : length, length);
  int32_t count = end - start;
  if (count > length - target) count = length - target;
  if (count <= 0 || target == start) return self;
  xt_value *temporary = (xt_value *)malloc(sizeof(xt_value) * (size_t)count);
  if (!temporary) return self;
  for (int32_t i = 0; i < count; i++) temporary[i] = xt_ta_element(self, (uint32_t)(start + i));
  for (int32_t i = 0; i < count; i++) xt_ta_store(self, kind, (uint32_t)(target + i), xt_to_number(temporary[i]));
  free(temporary);
  return self;
}

static xt_value xt_ta_method_at(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int32_t length = (int32_t)xt_ta_length(self);
  int32_t index = xt_to_int32(xt_arg_at(argc, argv, 0));
  if (index < 0) index += length;
  if (index < 0 || index >= length) return XT_UNDEFINED;
  return xt_ta_element(self, (uint32_t)index);
}

static xt_value xt_ta_method_keys(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value out = xt_array_new(0, NULL);
  for (int32_t i = 0; i < length; i++) xt_array_push(out, xt_number((double)i));
  return out;
}

static xt_value xt_ta_method_values(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value out = xt_array_new(0, NULL);
  for (int32_t i = 0; i < length; i++) xt_array_push(out, xt_ta_element(self, (uint32_t)i));
  return out;
}

static xt_value xt_ta_method_entries(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value out = xt_array_new(0, NULL);
  for (int32_t i = 0; i < length; i++) {
    xt_value pair = xt_array_new(0, NULL);
    xt_array_push(pair, xt_number((double)i));
    xt_array_push(pair, xt_ta_element(self, (uint32_t)i));
    xt_array_push(out, pair);
  }
  return out;
}
