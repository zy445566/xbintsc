/* -- Map ------------------------------------------------------------------ */

int xt_is_map(xt_value value) {
  return XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_MAP;
}

int32_t xt_map_size(xt_value value) {
  if (!xt_is_map(value)) return 0;
  return (int32_t)((xt_map *)XT_GET_PTR(value))->count;
}

static int32_t xt_map_index(xt_map *map, xt_value key) {
  for (uint32_t i = 0; i < map->count; i++) if (xt_value_equals(map->keys[i], key)) return (int32_t)i;
  return -1;
}

static void xt_map_set(xt_map *map, xt_value key, xt_value value) {
  int32_t index = xt_map_index(map, key);
  if (index >= 0) { map->values[index] = value; return; }
  if (map->count >= map->capacity) {
    uint32_t capacity = map->capacity == 0 ? 8 : map->capacity * 2;
    map->keys = (xt_value *)realloc(map->keys, sizeof(xt_value) * capacity);
    map->values = (xt_value *)realloc(map->values, sizeof(xt_value) * capacity);
    if (!map->keys || !map->values) abort();
    map->capacity = capacity;
  }
  map->keys[map->count] = key;
  map->values[map->count] = value;
  map->count++;
}

static int xt_map_delete(xt_map *map, xt_value key) {
  int32_t index = xt_map_index(map, key);
  if (index < 0) return 0;
  for (uint32_t i = (uint32_t)index + 1; i < map->count; i++) {
    map->keys[i - 1] = map->keys[i];
    map->values[i - 1] = map->values[i];
  }
  map->count--;
  return 1;
}

xt_value xt_map_ctor(int32_t argc, xt_value *argv) {
  xt_map *map = (xt_map *)xt_alloc(sizeof(xt_map), XT_OBJECT_KIND_MAP);
  map->keys = NULL; map->values = NULL; map->count = 0; map->capacity = 0;
  xt_value iterable = xt_arg_at(argc, argv, 0);
  if (XT_IS_ARRAY(iterable)) {
    xt_array *array = xt_as_array(iterable);
    for (uint32_t i = 0; i < array->length; i++) {
      xt_array *pair = xt_as_array(array->items[i]);
      if (pair && pair->length >= 2) xt_map_set(map, pair->items[0], pair->items[1]);
    }
  }
  return XT_FROM_PTR(XT_TAG_OBJECT, map);
}

static xt_value xt_map_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  xt_map *map = (xt_map *)XT_GET_PTR(target);
  if (strcmp(method, "set") == 0) { xt_map_set(map, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1)); return target; }
  if (strcmp(method, "get") == 0) {
    int32_t index = xt_map_index(map, xt_arg_at(argc, argv, 0));
    return index >= 0 ? map->values[index] : XT_UNDEFINED;
  }
  if (strcmp(method, "has") == 0) return xt_bool(xt_map_index(map, xt_arg_at(argc, argv, 0)) >= 0);
  if (strcmp(method, "delete") == 0) return xt_bool(xt_map_delete(map, xt_arg_at(argc, argv, 0)));
  if (strcmp(method, "clear") == 0) { map->count = 0; return XT_UNDEFINED; }
  if (strcmp(method, "forEach") == 0) {
    xt_value fn = xt_arg_at(argc, argv, 0);
    for (uint32_t i = 0; i < map->count; i++) { xt_value args[3] = {map->values[i], map->keys[i], target}; xt_closure_call(fn, 3, args); }
    return XT_UNDEFINED;
  }
  if (strcmp(method, "keys") == 0) {
    xt_value out = xt_array_new(0, NULL);
    for (uint32_t i = 0; i < map->count; i++) xt_array_push(out, map->keys[i]);
    return out;
  }
  if (strcmp(method, "values") == 0) {
    xt_value out = xt_array_new(0, NULL);
    for (uint32_t i = 0; i < map->count; i++) xt_array_push(out, map->values[i]);
    return out;
  }
  if (strcmp(method, "entries") == 0) {
    xt_value out = xt_array_new(0, NULL);
    for (uint32_t i = 0; i < map->count; i++) {
      xt_value pair = xt_array_new(0, NULL);
      xt_array_push(pair, map->keys[i]);
      xt_array_push(pair, map->values[i]);
      xt_array_push(out, pair);
    }
    return out;
  }
  *handled = 0;
  return XT_UNDEFINED;
}
