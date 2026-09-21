/* -- array methods -------------------------------------------------------- */

static xt_value xt_array_pop(xt_value value) {
  xt_array *array = xt_as_array(value);
  if (!array || array->length == 0) return XT_UNDEFINED;
  return array->items[--array->length];
}

static xt_value xt_array_shift(xt_value value) {
  xt_array *array = xt_as_array(value);
  if (!array || array->length == 0) return XT_UNDEFINED;
  xt_value first = array->items[0];
  for (uint32_t i = 1; i < array->length; i++) array->items[i - 1] = array->items[i];
  array->length--;
  return first;
}

static xt_value xt_array_unshift(xt_value value, int32_t argc, xt_value *argv) {
  xt_array *array = xt_as_array(value);
  if (!array) return XT_UNDEFINED;
  if (argc <= 0) return xt_number((double)array->length);
  xt_array_reserve(array, array->length + (uint32_t)argc);
  for (uint32_t i = array->length; i-- > 0;) array->items[i + argc] = array->items[i];
  for (int32_t i = 0; i < argc; i++) array->items[i] = argv[i];
  array->length += (uint32_t)argc;
  return xt_number((double)array->length);
}

static void xt_buffer_append(char **out, size_t *length, size_t *capacity, const char *data, size_t size) {
  if (*length + size + 1 > *capacity) {
    while (*length + size + 1 > *capacity) *capacity = (*capacity) * 2;
    *out = (char *)realloc(*out, *capacity);
    if (!*out) abort();
  }
  memcpy(*out + *length, data, size);
  *length += size;
  (*out)[*length] = '\0';
}

static xt_value xt_array_join(xt_value value, xt_value separator) {
  xt_array *array = xt_as_array(value);
  if (!array) return xt_string_from_cstr("");
  xt_string *sep = xt_as_string(xt_to_string(separator == XT_UNDEFINED ? xt_string_from_cstr(",") : separator));
  size_t capacity = 32;
  size_t length = 0;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  out[0] = '\0';
  for (uint32_t i = 0; i < array->length; i++) {
    if (i > 0) xt_buffer_append(&out, &length, &capacity, sep->data, sep->length);
    xt_value item = array->items[i];
    if (item == XT_UNDEFINED || item == XT_NULL) continue;
    xt_string *str = xt_as_string(xt_to_string(item));
    xt_buffer_append(&out, &length, &capacity, str->data, str->length);
  }
  xt_value result = xt_string_new(out, length);
  free(out);
  return result;
}

static void xt_normalize_slice(int32_t len, int32_t *start, int32_t *end) {
  if (*start < 0) *start += len;
  if (*start < 0) *start = 0;
  if (*start > len) *start = len;
  if (*end < 0) *end += len;
  if (*end < 0) *end = 0;
  if (*end > len) *end = len;
  if (*end < *start) *end = *start;
}

static xt_value xt_array_slice(xt_value value, xt_value startValue, xt_value endValue) {
  xt_array *array = xt_as_array(value);
  if (!array) return XT_UNDEFINED;
  int32_t len = (int32_t)array->length;
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  int32_t end = endValue == XT_UNDEFINED ? len : xt_to_int32(endValue);
  xt_normalize_slice(len, &start, &end);
  return xt_array_new(end - start, array->items + start);
}

static xt_value xt_array_index_of(xt_value value, xt_value needle, xt_value fromValue, int sameValueZero) {
  xt_array *array = xt_as_array(value);
  if (!array) return xt_number(-1);
  int32_t from = fromValue == XT_UNDEFINED ? 0 : xt_to_int32(fromValue);
  if (from < 0) from += (int32_t)array->length;
  if (from < 0) from = 0;
  for (uint32_t i = (uint32_t)from; i < array->length; i++) {
    int equal = sameValueZero ? xt_same_value_zero(array->items[i], needle) : xt_truthy(xt_seq(array->items[i], needle));
    if (equal) return xt_number((double)i);
  }
  return xt_number(-1);
}

static xt_value xt_array_concat(xt_value value, int32_t argc, xt_value *argv) {
  xt_array *array = xt_as_array(value);
  xt_value result = xt_array_new(0, NULL);
  if (!array) return result;
  for (uint32_t i = 0; i < array->length; i++) xt_array_push(result, array->items[i]);
  for (int32_t i = 0; i < argc; i++) {
    if (XT_IS_ARRAY(argv[i])) {
      xt_array *other = xt_as_array(argv[i]);
      for (uint32_t j = 0; j < other->length; j++) xt_array_push(result, other->items[j]);
    } else {
      xt_array_push(result, argv[i]);
    }
  }
  return result;
}

static xt_value xt_array_for_each(xt_value value, xt_value fn) {
  xt_array *array = xt_as_array(value);
  if (!array) return XT_UNDEFINED;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value args[3] = {array->items[i], xt_number((double)i), value};
    xt_closure_call(fn, 3, args);
  }
  return XT_UNDEFINED;
}

static xt_value xt_array_map(xt_value value, xt_value fn) {
  xt_array *array = xt_as_array(value);
  xt_value result = xt_array_new(0, NULL);
  if (!array) return result;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value args[3] = {array->items[i], xt_number((double)i), value};
    xt_array_push(result, xt_closure_call(fn, 3, args));
  }
  return result;
}

static xt_value xt_array_filter(xt_value value, xt_value fn) {
  xt_array *array = xt_as_array(value);
  xt_value result = xt_array_new(0, NULL);
  if (!array) return result;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value args[3] = {array->items[i], xt_number((double)i), value};
    if (xt_truthy(xt_closure_call(fn, 3, args))) xt_array_push(result, array->items[i]);
  }
  return result;
}

static xt_value xt_array_reduce(xt_value value, xt_value fn, int32_t hasInitial, xt_value initial) {
  xt_array *array = xt_as_array(value);
  if (!array) return XT_UNDEFINED;
  uint32_t i = 0;
  xt_value accumulator;
  if (hasInitial) {
    accumulator = initial;
  } else if (array->length > 0) {
    accumulator = array->items[0];
    i = 1;
  } else {
    xt_throw(xt_string_from_cstr("TypeError: Reduce of empty array with no initial value"));
    return XT_UNDEFINED;
  }
  for (; i < array->length; i++) {
    xt_value args[4] = {accumulator, array->items[i], xt_number((double)i), value};
    accumulator = xt_closure_call(fn, 4, args);
  }
  return accumulator;
}
