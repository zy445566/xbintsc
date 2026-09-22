/* -- Set ------------------------------------------------------------------ */

int xt_is_set(xt_value value) {
  return XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_SET;
}

int32_t xt_set_size(xt_value value) {
  if (!xt_is_set(value)) return 0;
  return (int32_t)((xt_set_object *)XT_GET_PTR(value))->count;
}

/* -- for...of iteration --------------------------------------------------- */

/* `for...of` lowers to a per-step check plus an index lookup. Arrays and
   strings index directly; Maps yield `[key, value]` pairs and Sets yield their
   values. Generators pull their next value during the check. */
xt_value xt_iter_length(xt_value value) {
  if (xt_is_map(value)) return xt_number((double)xt_map_size(value));
  if (xt_is_set(value)) return xt_number((double)xt_set_size(value));
  return xt_array_length(value);
}

/* Resolved `[Symbol.iterator]()` result: primes one `next()` per step and
 * caches its `value` until `xt_iter_value` consumes it. */
typedef struct {
  xt_header header;
  xt_value iterator;
  xt_value cached;
  int32_t done;
} xt_iterator_state;

static int xt_is_iterator_state(xt_value value) {
  if (!XT_IS_OBJECT(value)) return 0;
  return ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_ITERATOR;
}

/* Resolve the iteration protocol once, before the loop starts. Arrays,
 * strings, Maps, Sets and generators keep their built-in index protocol;
 * any other object with a `Symbol.iterator` method is wrapped. */
xt_value xt_iter_open(xt_value value) {
  if (XT_IS_ARRAY(value) || XT_IS_STRING(value) || xt_is_map(value) || xt_is_set(value) ||
      xt_is_generator(value))
    return value;
  if (XT_IS_OBJECT(value)) {
    xt_value method = xt_object_get(value, xt_symbol_well_known("iterator"));
    if (XT_IS_FUNCTION(method)) {
      xt_value iterator = xt_call_with_this(method, value, 0, NULL);
      xt_iterator_state *state =
          (xt_iterator_state *)xt_alloc(sizeof(xt_iterator_state), XT_OBJECT_KIND_ITERATOR);
      state->iterator = iterator;
      state->cached = XT_UNDEFINED;
      state->done = 0;
      return XT_FROM_PTR(XT_TAG_OBJECT, state);
    }
  }
  xt_throw(xt_string_from_cstr("TypeError: value is not iterable"));
  return XT_UNDEFINED;
}

xt_value xt_iter_has(xt_value value, xt_value index) {
  if (xt_is_iterator_state(value)) {
    xt_iterator_state *state = (xt_iterator_state *)XT_GET_PTR(value);
    if (state->done) return XT_FALSE;
    xt_value next = xt_get(state->iterator, xt_string_from_cstr("next"));
    xt_value result = xt_call_with_this(next, state->iterator, 0, NULL);
    state->cached = xt_get(result, xt_string_from_cstr("value"));
    state->done = xt_truthy(xt_get(result, xt_string_from_cstr("done")));
    return xt_bool(!state->done);
  }
  if (xt_is_generator(value)) return xt_bool(xt_generator_has_next(value));
  if (xt_is_map(value)) return xt_bool(xt_to_int32(index) < xt_map_size(value));
  if (xt_is_set(value)) return xt_bool(xt_to_int32(index) < xt_set_size(value));
  return xt_bool(xt_to_int32(index) < xt_to_int32(xt_array_length(value)));
}

xt_value xt_iter_value(xt_value value, xt_value index) {
  if (xt_is_iterator_state(value)) return ((xt_iterator_state *)XT_GET_PTR(value))->cached;
  if (xt_is_generator(value)) return xt_generator_iter_value(value);
  if (xt_is_map(value)) {
    xt_map *map = (xt_map *)XT_GET_PTR(value);
    int64_t i = (int64_t)xt_to_int32(index);
    if (i < 0 || i >= (int64_t)map->count) return XT_UNDEFINED;
    xt_value pair = xt_array_new(0, NULL);
    xt_array_push(pair, map->keys[i]);
    xt_array_push(pair, map->values[i]);
    return pair;
  }
  if (xt_is_set(value)) {
    xt_set_object *set = (xt_set_object *)XT_GET_PTR(value);
    int64_t i = (int64_t)xt_to_int32(index);
    if (i < 0 || i >= (int64_t)set->count) return XT_UNDEFINED;
    return set->values[i];
  }
  return xt_get(value, index);
}

static int32_t xt_set_index(xt_set_object *set, xt_value key) {
  for (uint32_t i = 0; i < set->count; i++) if (xt_value_equals(set->values[i], key)) return (int32_t)i;
  return -1;
}

static void xt_set_add(xt_set_object *set, xt_value value) {
  if (xt_set_index(set, value) >= 0) return;
  if (set->count >= set->capacity) {
    uint32_t capacity = set->capacity == 0 ? 8 : set->capacity * 2;
    set->values = (xt_value *)realloc(set->values, sizeof(xt_value) * capacity);
    if (!set->values) abort();
    set->capacity = capacity;
  }
  set->values[set->count++] = value;
}

static int xt_set_delete(xt_set_object *set, xt_value value) {
  int32_t index = xt_set_index(set, value);
  if (index < 0) return 0;
  for (uint32_t i = (uint32_t)index + 1; i < set->count; i++) set->values[i - 1] = set->values[i];
  set->count--;
  return 1;
}

xt_value xt_set_ctor(int32_t argc, xt_value *argv) {
  xt_set_object *set = (xt_set_object *)xt_alloc(sizeof(xt_set_object), XT_OBJECT_KIND_SET);
  set->values = NULL; set->count = 0; set->capacity = 0;
  xt_value iterable = xt_arg_at(argc, argv, 0);
  if (XT_IS_ARRAY(iterable)) {
    xt_array *array = xt_as_array(iterable);
    for (uint32_t i = 0; i < array->length; i++) xt_set_add(set, array->items[i]);
  }
  return XT_FROM_PTR(XT_TAG_OBJECT, set);
}

static xt_value xt_set_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  xt_set_object *set = (xt_set_object *)XT_GET_PTR(target);
  if (strcmp(method, "add") == 0) { xt_set_add(set, xt_arg_at(argc, argv, 0)); return target; }
  if (strcmp(method, "has") == 0) return xt_bool(xt_set_index(set, xt_arg_at(argc, argv, 0)) >= 0);
  if (strcmp(method, "delete") == 0) return xt_bool(xt_set_delete(set, xt_arg_at(argc, argv, 0)));
  if (strcmp(method, "clear") == 0) { set->count = 0; return XT_UNDEFINED; }
  if (strcmp(method, "forEach") == 0) {
    xt_value fn = xt_arg_at(argc, argv, 0);
    for (uint32_t i = 0; i < set->count; i++) { xt_value args[3] = {set->values[i], set->values[i], target}; xt_closure_call(fn, 3, args); }
    return XT_UNDEFINED;
  }
  if (strcmp(method, "values") == 0 || strcmp(method, "keys") == 0) {
    xt_value out = xt_array_new(0, NULL);
    for (uint32_t i = 0; i < set->count; i++) xt_array_push(out, set->values[i]);
    return out;
  }
  if (strcmp(method, "entries") == 0) {
    xt_value out = xt_array_new(0, NULL);
    for (uint32_t i = 0; i < set->count; i++) {
      xt_value pair = xt_array_new(0, NULL);
      xt_array_push(pair, set->values[i]);
      xt_array_push(pair, set->values[i]);
      xt_array_push(out, pair);
    }
    return out;
  }
  *handled = 0;
  return XT_UNDEFINED;
}
