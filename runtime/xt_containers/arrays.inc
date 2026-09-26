/* ------------------------------------------------------------------------- */
/* Arrays                                                                    */
/* ------------------------------------------------------------------------- */

void xt_array_reserve(xt_array *array, uint32_t needed) {
  if (needed <= array->capacity) return;
  uint32_t capacity = array->capacity ? array->capacity * 2 : 8;
  while (capacity < needed) capacity *= 2;
  array->items = (xt_value *)realloc(array->items, sizeof(xt_value) * capacity);
  if (!array->items) {
    fprintf(stderr, "xbintsc: out of memory growing array\n");
    abort();
  }
  for (uint32_t i = array->capacity; i < capacity; i++) array->items[i] = XT_UNDEFINED;
  array->capacity = capacity;
}

xt_value xt_array_new(int32_t count, xt_value *items) {
  xt_array *array = (xt_array *)xt_alloc(sizeof(xt_array), XT_OBJECT_KIND_ARRAY);
  array->length = 0;
  array->capacity = 0;
  array->items = NULL;
  array->extra = XT_UNDEFINED;
  if (count > 0) {
    xt_array_reserve(array, (uint32_t)count);
    for (int32_t i = 0; i < count; i++) array->items[i] = items[i];
    array->length = (uint32_t)count;
  }
  return XT_FROM_PTR(XT_TAG_ARRAY, array);
}

/* Named properties of an array live in a lazily-created side object. Returns
 * XT_UNDEFINED when `create` is false and no such object exists yet. */
static xt_value xt_array_extra(xt_array *array, int create) {
  if (!XT_IS_OBJECT(array->extra)) {
    if (!create) return XT_UNDEFINED;
    array->extra = xt_object_new();
  }
  return array->extra;
}

xt_value xt_array_get(xt_value value, xt_value index) {
  if (!XT_IS_ARRAY(value)) return XT_UNDEFINED;
  xt_array *array = (xt_array *)XT_GET_PTR(value);
  int32_t i = xt_to_int32(index);
  if (i < 0 || (uint32_t)i >= array->length) return XT_UNDEFINED;
  return array->items[i];
}

xt_value xt_array_set(xt_value value, xt_value index, xt_value newValue) {
  if (!XT_IS_ARRAY(value)) return newValue;
  xt_array *array = (xt_array *)XT_GET_PTR(value);
  int32_t i = xt_to_int32(index);
  if (i < 0) return newValue;
  xt_array_reserve(array, (uint32_t)i + 1);
  for (uint32_t j = array->length; j < (uint32_t)i; j++) array->items[j] = XT_UNDEFINED;
  array->items[i] = newValue;
  if ((uint32_t)i >= array->length) array->length = (uint32_t)i + 1;
  return newValue;
}

xt_value xt_array_push(xt_value value, xt_value newValue) {
  if (!XT_IS_ARRAY(value)) return xt_number(0);
  xt_array *array = (xt_array *)XT_GET_PTR(value);
  xt_array_reserve(array, array->length + 1);
  array->items[array->length++] = newValue;
  return xt_number((double)array->length);
}

xt_value xt_array_length(xt_value value) {
  if (XT_IS_ARRAY(value)) return xt_number((double)((xt_array *)XT_GET_PTR(value))->length);
  if (XT_IS_STRING(value)) return xt_number((double)xt_string_length(xt_as_string(value)));
  /* Functions expose their arity as `.length`. */
  if (XT_IS_FUNCTION(value)) {
    int32_t arity = ((xt_function *)XT_GET_PTR(value))->arity;
    return xt_number(arity >= 0 ? (double)arity : 0.0);
  }
  /* Objects such as Buffers expose a `length` property of their own. */
  if (XT_IS_OBJECT(value)) return xt_object_get_cstr(value, "length");
  return XT_UNDEFINED;
}

xt_value xt_array_spread(xt_value target, xt_value source) {
  if (!XT_IS_ARRAY(target)) return target;
  if (XT_IS_ARRAY(source)) {
    xt_array *dst = (xt_array *)XT_GET_PTR(target);
    xt_array *src = (xt_array *)XT_GET_PTR(source);
    xt_array_reserve(dst, dst->length + src->length);
    for (uint32_t i = 0; i < src->length; i++) dst->items[dst->length++] = src->items[i];
    return target;
  }
  /* Spreading a string/Map/Set/iterable iterates exactly like `for...of`. */
  source = xt_iter_open(source);
  for (int32_t i = 0; xt_truthy(xt_iter_has(source, xt_number((double)i))); i++) {
    xt_array_push(target, xt_iter_value(source, xt_number((double)i)));
  }
  return target;
}

int32_t xt_array_size(xt_value value) {
  if (!XT_IS_ARRAY(value)) return 0;
  return (int32_t)((xt_array *)XT_GET_PTR(value))->length;
}

xt_value *xt_array_items(xt_value value) {
  if (!XT_IS_ARRAY(value)) return NULL;
  return ((xt_array *)XT_GET_PTR(value))->items;
}
