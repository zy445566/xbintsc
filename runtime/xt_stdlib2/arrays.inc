/* -- array methods -------------------------------------------------------- */

static xt_value xt_array_at(xt_value value, xt_value indexValue) {
  xt_array *array = xt_as_array(value);
  if (!array) return XT_UNDEFINED;
  int32_t index = indexValue == XT_UNDEFINED ? 0 : xt_to_int32(indexValue);
  if (index < 0) index += (int32_t)array->length;
  if (index < 0 || (uint32_t)index >= array->length) return XT_UNDEFINED;
  return array->items[index];
}

static xt_value xt_array_find(xt_value value, xt_value fn, int mode) {
  xt_array *array = xt_as_array(value);
  if (!array) return (mode == 0 || mode == 2) ? XT_UNDEFINED : xt_number(-1);
  int reverse = (mode == 2 || mode == 3);
  for (uint32_t step = 0; step < array->length; step++) {
    int32_t i = reverse ? (int32_t)(array->length - 1 - step) : (int32_t)step;
    xt_value args[3] = {array->items[i], xt_number((double)i), value};
    if (xt_truthy(xt_closure_call(fn, 3, args))) return (mode == 0 || mode == 2) ? array->items[i] : xt_number((double)i);
  }
  return (mode == 0 || mode == 2) ? XT_UNDEFINED : xt_number(-1);
}

static xt_value xt_array_quantifier(xt_value value, xt_value fn, int every) {
  xt_array *array = xt_as_array(value);
  if (!array) return xt_bool(every ? 1 : 0);
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value args[3] = {array->items[i], xt_number((double)i), value};
    int ok = xt_truthy(xt_closure_call(fn, 3, args));
    if (every && !ok) return XT_FALSE;
    if (!every && ok) return XT_TRUE;
  }
  return xt_bool(every ? 1 : 0);
}

/*
 * Default `Array.prototype.sort` comparator. Per spec the elements are
 * converted to strings and compared by UTF-16 code unit; `undefined` always
 * sorts last and holes are handled by the caller. Because our strings are
 * UTF-8, a byte-wise compare matches UTF-16 ordering for the BMP (and differs
 * only for supplementary vs. U+E000..U+FFFF code points).
 */
static int xt_array_default_compare(xt_value a, xt_value b) {
  if (a == XT_UNDEFINED) return b == XT_UNDEFINED ? 0 : 1;
  if (b == XT_UNDEFINED) return -1;
  xt_string *sa = xt_as_string(xt_to_string(a));
  xt_string *sb = xt_as_string(xt_to_string(b));
  uint32_t n = sa->length < sb->length ? sa->length : sb->length;
  int cmp = n ? memcmp(sa->data, sb->data, n) : 0;
  if (cmp != 0) return cmp < 0 ? -1 : 1;
  if (sa->length == sb->length) return 0;
  return sa->length < sb->length ? -1 : 1;
}

static int xt_array_sort_compare(xt_value fn, int hasFn, xt_value a, xt_value b) {
  if (!hasFn) return xt_array_default_compare(a, b);
  xt_value args[2] = {a, b};
  double result = xt_to_number(xt_closure_call(fn, 2, args));
  if (result < 0) return -1;
  if (result > 0) return 1;
  return 0;
}

/* Stable bottom-up merge sort: O(n log n) and never reorders equal keys. */
static void xt_array_merge_sort(xt_value *items, xt_value *scratch, uint32_t length, xt_value fn, int hasFn) {
  for (uint32_t width = 1; width < length; width *= 2) {
    for (uint32_t lo = 0; lo < length; lo += 2 * width) {
      uint32_t mid = lo + width < length ? lo + width : length;
      uint32_t hi = lo + 2 * width < length ? lo + 2 * width : length;
      uint32_t i = lo, j = mid, k = lo;
      while (i < mid && j < hi) {
        if (xt_array_sort_compare(fn, hasFn, items[j], items[i]) < 0) scratch[k++] = items[j++];
        else scratch[k++] = items[i++];
      }
      while (i < mid) scratch[k++] = items[i++];
      while (j < hi) scratch[k++] = items[j++];
      for (k = lo; k < hi; k++) items[k] = scratch[k];
    }
  }
}

static xt_value xt_array_sort(xt_value value, xt_value fn) {
  xt_array *array = xt_as_array(value);
  if (!array || array->length < 2) return value;
  int hasFn = XT_IS_FUNCTION(fn);
  xt_value *scratch = (xt_value *)malloc(sizeof(xt_value) * array->length);
  if (!scratch) {
    xt_throw(xt_string_from_cstr("RangeError: out of memory in Array.prototype.sort"));
    return value;
  }
  xt_array_merge_sort(array->items, scratch, array->length, fn, hasFn);
  free(scratch);
  return value;
}

static void xt_array_flatten_into(xt_value out, xt_value value, int depth) {
  xt_array *array = xt_as_array(value);
  if (!array) { xt_array_push(out, value); return; }
  for (uint32_t i = 0; i < array->length; i++) {
    if (depth > 0 && XT_IS_ARRAY(array->items[i])) xt_array_flatten_into(out, array->items[i], depth - 1);
    else xt_array_push(out, array->items[i]);
  }
}

static xt_value xt_array_flat_map(xt_value value, xt_value fn) {
  xt_array *array = xt_as_array(value);
  xt_value out = xt_array_new(0, NULL);
  if (!array) return out;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value args[3] = {array->items[i], xt_number((double)i), value};
    xt_value mapped = xt_closure_call(fn, 3, args);
    if (XT_IS_ARRAY(mapped)) {
      xt_array *m = xt_as_array(mapped);
      for (uint32_t j = 0; j < m->length; j++) xt_array_push(out, m->items[j]);
    } else xt_array_push(out, mapped);
  }
  return out;
}

static xt_value xt_array_last_index_of(xt_value value, xt_value needle, xt_value fromValue) {
  xt_array *array = xt_as_array(value);
  if (!array) return xt_number(-1);
  int32_t from = fromValue == XT_UNDEFINED ? (int32_t)array->length - 1 : xt_to_int32(fromValue);
  if (from < 0) from += (int32_t)array->length;
  if (from >= (int32_t)array->length) from = (int32_t)array->length - 1;
  for (int32_t i = from; i >= 0; i--) if (xt_value_equals(array->items[i], needle)) return xt_number((double)i);
  return xt_number(-1);
}

static xt_value xt_array_fill(xt_value value, xt_value fillValue, xt_value startValue, xt_value endValue) {
  xt_array *array = xt_as_array(value);
  if (!array) return value;
  int32_t len = (int32_t)array->length;
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  int32_t end = endValue == XT_UNDEFINED ? len : xt_to_int32(endValue);
  if (start < 0) start += len;
  if (end < 0) end += len;
  if (start < 0) start = 0;
  if (end > len) end = len;
  for (int32_t i = start; i < end; i++) array->items[i] = fillValue;
  return value;
}

static xt_value xt_array_copy_within(xt_value value, xt_value targetValue, xt_value startValue, xt_value endValue) {
  xt_array *array = xt_as_array(value);
  if (!array) return value;
  int32_t len = (int32_t)array->length;
  int32_t target = xt_to_int32(targetValue == XT_UNDEFINED ? xt_number(0) : targetValue);
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  int32_t end = endValue == XT_UNDEFINED ? len : xt_to_int32(endValue);
  if (target < 0) target += len;
  if (start < 0) start += len;
  if (end < 0) end += len;
  if (target < 0 || start < 0 || target >= len || start >= len) return value;
  if (end > len) end = len;
  int32_t count = end - start;
  if (count > len - target) count = len - target;
  if (count <= 0) return value;
  memmove(array->items + target, array->items + start, sizeof(xt_value) * (size_t)count);
  return value;
}

static xt_value xt_array_splice(xt_value value, int32_t argc, xt_value *argv) {
  xt_array *array = xt_as_array(value);
  if (!array) return XT_UNDEFINED;
  int32_t len = (int32_t)array->length;
  int32_t start = argc >= 1 ? xt_to_int32(argv[0]) : 0;
  if (start < 0) start += len;
  if (start < 0) start = 0;
  if (start > len) start = len;
  int32_t deleteCount;
  if (argc < 2) {
    deleteCount = len - start;
  } else {
    deleteCount = xt_to_int32(argv[1]);
    if (deleteCount < 0) deleteCount = 0;
    if (deleteCount > len - start) deleteCount = len - start;
  }
  int32_t insertCount = argc > 2 ? argc - 2 : 0;
  xt_value removed = xt_array_new(deleteCount, deleteCount > 0 ? array->items + start : NULL);
  int32_t newLength = len - deleteCount + insertCount;
  xt_array_reserve(array, (uint32_t)(newLength > 0 ? newLength : 0));
  if (insertCount != deleteCount) {
    memmove(array->items + start + insertCount, array->items + start + deleteCount,
            sizeof(xt_value) * (size_t)(len - start - deleteCount));
  }
  for (int32_t i = 0; i < insertCount; i++) array->items[start + i] = argv[2 + i];
  array->length = (uint32_t)newLength;
  return removed;
}

/* Copy the own elements of `value` into a fresh array. */
static xt_value xt_array_copy(xt_value value) {
  xt_value out = xt_array_new(0, NULL);
  xt_array *array = xt_as_array(value);
  if (!array) return out;
  for (uint32_t i = 0; i < array->length; i++) xt_array_push(out, array->items[i]);
  return out;
}

static xt_value xt_array_to_reversed(xt_value value) {
  xt_value copy = xt_array_copy(value);
  xt_array *array = xt_as_array(copy);
  for (uint32_t i = 0; i + 1 < array->length - i; i++) {
    xt_value tmp = array->items[i];
    array->items[i] = array->items[array->length - 1 - i];
    array->items[array->length - 1 - i] = tmp;
  }
  return copy;
}

static xt_value xt_array_to_sorted(xt_value value, xt_value fn) {
  return xt_array_sort(xt_array_copy(value), fn);
}

static xt_value xt_array_to_spliced(xt_value value, int32_t argc, xt_value *argv) {
  xt_value copy = xt_array_copy(value);
  xt_array_splice(copy, argc, argv);
  return copy;
}

static xt_value xt_array_with(xt_value value, xt_value indexValue, xt_value newValue) {
  xt_value copy = xt_array_copy(value);
  xt_array *array = xt_as_array(copy);
  int32_t index = xt_to_int32(indexValue);
  if (index < 0) index += (int32_t)array->length;
  if (index < 0 || index >= (int32_t)array->length) {
    xt_throw(xt_add(xt_string_from_cstr("RangeError: Invalid index : "), xt_to_string(indexValue)));
    return copy;
  }
  array->items[index] = newValue;
  return copy;
}

xt_value xt_ext_array_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "at") == 0) return xt_array_at(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "find") == 0) return xt_array_find(target, xt_arg_at(argc, argv, 0), 0);
  if (strcmp(method, "findIndex") == 0) return xt_array_find(target, xt_arg_at(argc, argv, 0), 1);
  if (strcmp(method, "findLast") == 0) return xt_array_find(target, xt_arg_at(argc, argv, 0), 2);
  if (strcmp(method, "findLastIndex") == 0) return xt_array_find(target, xt_arg_at(argc, argv, 0), 3);
  if (strcmp(method, "some") == 0) return xt_array_quantifier(target, xt_arg_at(argc, argv, 0), 0);
  if (strcmp(method, "every") == 0) return xt_array_quantifier(target, xt_arg_at(argc, argv, 0), 1);
  if (strcmp(method, "sort") == 0) return xt_array_sort(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "flat") == 0) {
    double depth = xt_arg_at(argc, argv, 0) == XT_UNDEFINED ? 1 : xt_to_number(argv[0]);
    if (depth < 0) depth = 0;
    xt_value out = xt_array_new(0, NULL);
    xt_array_flatten_into(out, target, (int)depth);
    return out;
  }
  if (strcmp(method, "flatMap") == 0) return xt_array_flat_map(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "lastIndexOf") == 0) return xt_array_last_index_of(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "fill") == 0) return xt_array_fill(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), xt_arg_at(argc, argv, 2));
  if (strcmp(method, "copyWithin") == 0) return xt_array_copy_within(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), xt_arg_at(argc, argv, 2));
  if (strcmp(method, "splice") == 0) return xt_array_splice(target, argc, argv);
  if (strcmp(method, "toReversed") == 0) return xt_array_to_reversed(target);
  if (strcmp(method, "toSorted") == 0) return xt_array_to_sorted(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "toSpliced") == 0) return xt_array_to_spliced(target, argc, argv);
  if (strcmp(method, "with") == 0) return xt_array_with(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "reduceRight") == 0) {
    xt_array *array = xt_as_array(target);
    if (!array) return XT_UNDEFINED;
    xt_value fn = xt_arg_at(argc, argv, 0);
    int32_t i = (int32_t)array->length - 1;
    xt_value acc;
    if (argc >= 2) acc = argv[1];
    else if (array->length > 0) acc = array->items[i--];
    else { xt_throw(xt_string_from_cstr("TypeError: Reduce of empty array with no initial value")); return XT_UNDEFINED; }
    for (; i >= 0; i--) {
      xt_value args[4] = {acc, array->items[i], xt_number((double)i), target};
      acc = xt_closure_call(fn, 4, args);
    }
    return acc;
  }
  if (strcmp(method, "toString") == 0) return xt_call_method(target, xt_string_from_cstr("join"), 0, NULL);
  if (strcmp(method, "keys") == 0 || strcmp(method, "values") == 0 || strcmp(method, "entries") == 0) {
    xt_array *array = xt_as_array(target);
    xt_value out = xt_array_new(0, NULL);
    if (!array) return out;
    int key = strcmp(method, "keys") == 0;
    int entries = strcmp(method, "entries") == 0;
    for (uint32_t i = 0; i < array->length; i++) {
      if (entries) {
        xt_value pair = xt_array_new(0, NULL);
        xt_array_push(pair, xt_number((double)i));
        xt_array_push(pair, array->items[i]);
        xt_array_push(out, pair);
      } else if (key) xt_array_push(out, xt_number((double)i));
      else xt_array_push(out, array->items[i]);
    }
    return out;
  }
  *handled = 0;
  return XT_UNDEFINED;
}
