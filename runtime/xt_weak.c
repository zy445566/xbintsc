/*
 * xbintsc runtime — weak collections.
 *
 * `WeakMap`, `WeakSet` and `WeakRef` are the only values whose references the
 * mark-sweep collector must *not* follow unconditionally. Their keys (and the
 * target of a WeakRef) are excluded from the ordinary trace; instead the
 * collector runs an ephemeron fixed point after marking:
 *
 *     repeat
 *       for every live WeakMap entry whose key is marked:
 *         mark the value
 *     until nothing new is marked
 *
 * A value is therefore retained only while its key is otherwise reachable, and
 * a key/value cycle that is only reachable through the WeakMap is collected.
 * WeakRef targets that stay unmarked after that fixed point are cleared.
 *
 * WeakMap/WeakSet entries are removed at sweep time, so a later `has()` never
 * observes a key whose object has been collected.
 */

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

static int xt_is_weak_target(xt_value value) {
  return XT_IS_OBJECT(value) || XT_IS_ARRAY(value) || XT_IS_FUNCTION(value);
}

static void xt_weak_key_error(void) {
  xt_throw(xt_error_from_message("TypeError: Invalid value used as weak map key"));
}

int xt_is_weak_map(xt_value value) {
  return XT_IS_OBJECT(value) &&
         ((xt_header *)XT_GET_PTR(value))->kind == XT_OBJECT_KIND_WEAK_MAP;
}

int xt_is_weak_set(xt_value value) {
  return XT_IS_OBJECT(value) &&
         ((xt_header *)XT_GET_PTR(value))->kind == XT_OBJECT_KIND_WEAK_SET;
}

int xt_is_weak_ref(xt_value value) {
  return XT_IS_OBJECT(value) &&
         ((xt_header *)XT_GET_PTR(value))->kind == XT_OBJECT_KIND_WEAK_REF;
}

/* -- WeakMap -------------------------------------------------------------- */

static int32_t xt_weak_map_index(xt_weak_map *map, xt_value key) {
  for (uint32_t i = 0; i < map->count; i++) {
    if (xt_value_equals(map->keys[i], key)) return (int32_t)i;
  }
  return -1;
}

static void xt_weak_map_set(xt_weak_map *map, xt_value key, xt_value value) {
  int32_t index = xt_weak_map_index(map, key);
  if (index >= 0) {
    map->values[index] = value;
    return;
  }
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

static int xt_weak_map_delete(xt_weak_map *map, xt_value key) {
  int32_t index = xt_weak_map_index(map, key);
  if (index < 0) return 0;
  for (uint32_t i = (uint32_t)index + 1; i < map->count; i++) {
    map->keys[i - 1] = map->keys[i];
    map->values[i - 1] = map->values[i];
  }
  map->count--;
  return 1;
}

xt_value xt_weak_map_ctor(int32_t argc, xt_value *argv) {
  xt_weak_map *map = (xt_weak_map *)xt_alloc(sizeof(xt_weak_map), XT_OBJECT_KIND_WEAK_MAP);
  map->keys = NULL;
  map->values = NULL;
  map->count = 0;
  map->capacity = 0;
  xt_value iterable = xt_arg_at(argc, argv, 0);
  if (XT_IS_ARRAY(iterable)) {
    xt_array *array = (xt_array *)XT_GET_PTR(iterable);
    for (uint32_t i = 0; i < array->length; i++) {
      xt_array *pair = xt_as_array(array->items[i]);
      if (!pair || pair->length < 2) {
        xt_throw(xt_error_from_message("TypeError: Iterator value is not an entry object"));
        return XT_UNDEFINED;
      }
      if (!xt_is_weak_target(pair->items[0])) {
        xt_weak_key_error();
        return XT_UNDEFINED;
      }
      xt_weak_map_set(map, pair->items[0], pair->items[1]);
    }
  }
  return XT_FROM_PTR(XT_TAG_OBJECT, map);
}

/* -- WeakSet -------------------------------------------------------------- */

static int32_t xt_weak_set_index(xt_weak_set *set, xt_value value) {
  for (uint32_t i = 0; i < set->count; i++) {
    if (xt_value_equals(set->values[i], value)) return (int32_t)i;
  }
  return -1;
}

static void xt_weak_set_add(xt_weak_set *set, xt_value value) {
  if (xt_weak_set_index(set, value) >= 0) return;
  if (set->count >= set->capacity) {
    uint32_t capacity = set->capacity == 0 ? 8 : set->capacity * 2;
    set->values = (xt_value *)realloc(set->values, sizeof(xt_value) * capacity);
    if (!set->values) abort();
    set->capacity = capacity;
  }
  set->values[set->count++] = value;
}

static int xt_weak_set_delete(xt_weak_set *set, xt_value value) {
  int32_t index = xt_weak_set_index(set, value);
  if (index < 0) return 0;
  for (uint32_t i = (uint32_t)index + 1; i < set->count; i++) {
    set->values[i - 1] = set->values[i];
  }
  set->count--;
  return 1;
}

xt_value xt_weak_set_ctor(int32_t argc, xt_value *argv) {
  xt_weak_set *set = (xt_weak_set *)xt_alloc(sizeof(xt_weak_set), XT_OBJECT_KIND_WEAK_SET);
  set->values = NULL;
  set->count = 0;
  set->capacity = 0;
  xt_value iterable = xt_arg_at(argc, argv, 0);
  if (XT_IS_ARRAY(iterable)) {
    xt_array *array = (xt_array *)XT_GET_PTR(iterable);
    for (uint32_t i = 0; i < array->length; i++) {
      if (!xt_is_weak_target(array->items[i])) {
        xt_weak_key_error();
        return XT_UNDEFINED;
      }
      xt_weak_set_add(set, array->items[i]);
    }
  }
  return XT_FROM_PTR(XT_TAG_OBJECT, set);
}

/* -- WeakRef -------------------------------------------------------------- */

xt_value xt_weak_ref_ctor(int32_t argc, xt_value *argv) {
  xt_value target = xt_arg_at(argc, argv, 0);
  if (!xt_is_weak_target(target)) {
    xt_throw(xt_error_from_message("TypeError: WeakRef target must be an object"));
    return XT_UNDEFINED;
  }
  xt_weak_ref *ref = (xt_weak_ref *)xt_alloc(sizeof(xt_weak_ref), XT_OBJECT_KIND_WEAK_REF);
  ref->target = target;
  return XT_FROM_PTR(XT_TAG_OBJECT, ref);
}

xt_value xt_weak_method(xt_value target, const char *method, int32_t argc, xt_value *argv,
                        int *handled) {
  *handled = 1;
  if (xt_is_weak_map(target)) {
    xt_weak_map *map = (xt_weak_map *)XT_GET_PTR(target);
    xt_value key = xt_arg_at(argc, argv, 0);
    if (strcmp(method, "set") == 0) {
      if (!xt_is_weak_target(key)) {
        xt_weak_key_error();
        return XT_UNDEFINED;
      }
      xt_weak_map_set(map, key, xt_arg_at(argc, argv, 1));
      return target;
    }
    if (strcmp(method, "get") == 0) {
      if (!xt_is_weak_target(key)) return XT_UNDEFINED;
      int32_t index = xt_weak_map_index(map, key);
      return index >= 0 ? map->values[index] : XT_UNDEFINED;
    }
    if (strcmp(method, "has") == 0) {
      return xt_bool(xt_is_weak_target(key) && xt_weak_map_index(map, key) >= 0);
    }
    if (strcmp(method, "delete") == 0) {
      return xt_bool(xt_is_weak_target(key) && xt_weak_map_delete(map, key));
    }
  } else if (xt_is_weak_set(target)) {
    xt_weak_set *set = (xt_weak_set *)XT_GET_PTR(target);
    xt_value value = xt_arg_at(argc, argv, 0);
    if (strcmp(method, "add") == 0) {
      if (!xt_is_weak_target(value)) {
        xt_weak_key_error();
        return XT_UNDEFINED;
      }
      xt_weak_set_add(set, value);
      return target;
    }
    if (strcmp(method, "has") == 0) {
      return xt_bool(xt_is_weak_target(value) && xt_weak_set_index(set, value) >= 0);
    }
    if (strcmp(method, "delete") == 0) {
      return xt_bool(xt_is_weak_target(value) && xt_weak_set_delete(set, value));
    }
  } else if (xt_is_weak_ref(target)) {
    if (strcmp(method, "deref") == 0) return ((xt_weak_ref *)XT_GET_PTR(target))->target;
  }
  *handled = 0;
  return XT_UNDEFINED;
}

/* -- collector integration ------------------------------------------------ */

static int xt_weak_heap_value(xt_value value) {
  return XT_IS_STRING(value) || XT_IS_OBJECT(value) || XT_IS_ARRAY(value) ||
         XT_IS_FUNCTION(value) || XT_IS_BIGINT(value);
}

int xt_weak_gc_process_once(void) {
  int changed = 0;
  for (xt_header *header = xt_gc_heap_head(); header; header = header->gc_next) {
    if ((header->flags & XT_GC_MARK) == 0) continue;
    if (header->kind != XT_OBJECT_KIND_WEAK_MAP) continue;
    xt_weak_map *map = (xt_weak_map *)header;
    for (uint32_t i = 0; i < map->count; i++) {
      if (!xt_gc_value_is_marked(map->keys[i])) continue;
      xt_value value = map->values[i];
      if (!xt_weak_heap_value(value)) continue;
      if (xt_gc_value_is_marked(value)) continue;
      xt_gc_mark_value(value);
      changed = 1;
    }
  }
  return changed;
}

void xt_weak_gc_clear(void) {
  for (xt_header *header = xt_gc_heap_head(); header; header = header->gc_next) {
    if ((header->flags & XT_GC_MARK) == 0) continue;
    if (header->kind != XT_OBJECT_KIND_WEAK_REF) continue;
    xt_weak_ref *ref = (xt_weak_ref *)header;
    if (!xt_gc_value_is_marked(ref->target)) ref->target = XT_UNDEFINED;
  }
}

void xt_weak_gc_compact(void) {
  for (xt_header *header = xt_gc_heap_head(); header; header = header->gc_next) {
    if ((header->flags & XT_GC_MARK) == 0) continue;
    if (header->kind == XT_OBJECT_KIND_WEAK_MAP) {
      xt_weak_map *map = (xt_weak_map *)header;
      uint32_t write = 0;
      for (uint32_t read = 0; read < map->count; read++) {
        if (!xt_gc_value_is_marked(map->keys[read])) continue;
        map->keys[write] = map->keys[read];
        map->values[write] = map->values[read];
        write++;
      }
      map->count = write;
    } else if (header->kind == XT_OBJECT_KIND_WEAK_SET) {
      xt_weak_set *set = (xt_weak_set *)header;
      uint32_t write = 0;
      for (uint32_t read = 0; read < set->count; read++) {
        if (!xt_gc_value_is_marked(set->values[read])) continue;
        set->values[write++] = set->values[read];
      }
      set->count = write;
    }
  }
}
