/*
 * xbintsc runtime — `structuredClone`.
 *
 * Deep-clones arrays, plain objects, Map, Set, Date, RegExp and typed arrays,
 * preserving shared references and cycles. Functions, symbols, promises,
 * generators and iterators cannot be cloned, matching the platform API.
 *
 * The clone table maps source pointer -> clone. It lives on the C stack for
 * the duration of the call; every clone is also reachable from its parent (or
 * the current frame) while it is being filled, so the conservative stack scan
 * keeps it alive across the allocations that happen during cloning.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  xt_value source;
  xt_value clone;
} xt_clone_entry;

typedef struct {
  xt_clone_entry *entries;
  uint32_t count;
  uint32_t capacity;
} xt_clone_context;

static _Noreturn void xt_clone_fail(const char *kind) {
  char message[160];
  snprintf(message, sizeof(message), "%s could not be cloned.", kind);
  xt_value argument = xt_string_from_cstr(message);
  xt_throw(xt_type_error_ctor(1, &argument));
  abort();
}

static xt_value xt_clone_lookup(xt_clone_context *context, xt_value source) {
  for (uint32_t i = 0; i < context->count; i++) {
    if (context->entries[i].source == source) return context->entries[i].clone;
  }
  return XT_UNDEFINED;
}

static void xt_clone_remember(xt_clone_context *context, xt_value source, xt_value clone) {
  if (context->count >= context->capacity) {
    uint32_t capacity = context->capacity == 0 ? 8 : context->capacity * 2;
    xt_clone_entry *entries =
        (xt_clone_entry *)realloc(context->entries, sizeof(xt_clone_entry) * capacity);
    if (!entries) abort();
    context->entries = entries;
    context->capacity = capacity;
  }
  context->entries[context->count].source = source;
  context->entries[context->count].clone = clone;
  context->count++;
}

static xt_value xt_clone_value(xt_clone_context *context, xt_value value) {
  if (value == XT_UNDEFINED || value == XT_NULL || XT_IS_BOOL(value) || XT_IS_NUMBER(value) ||
      XT_IS_STRING(value) || XT_IS_BIGINT(value)) {
    return value;
  }
  if (XT_IS_FUNCTION(value)) xt_clone_fail("function");
  if (xt_is_symbol(value)) xt_clone_fail("symbol");

  xt_value existing = xt_clone_lookup(context, value);
  if (existing != XT_UNDEFINED) return existing;

  /* A typed array's own `slice` already produces an element-wise copy of the
   * same concrete type. */
  if (xt_typed_array_kind_of(value) >= 0) {
    return xt_call_method(value, xt_string_from_cstr("slice"), 0, NULL);
  }

  if (XT_IS_ARRAY(value)) {
    xt_value clone = xt_array_new(0, NULL);
    xt_clone_remember(context, value, clone);
    xt_array *array = xt_as_array(value);
    for (uint32_t i = 0; i < array->length; i++) {
      xt_array_push(clone, xt_clone_value(context, array->items[i]));
    }
    return clone;
  }

  if (!XT_IS_OBJECT(value)) xt_clone_fail("value");
  xt_object *object = (xt_object *)XT_GET_PTR(value);

  if (object->header.kind == XT_OBJECT_KIND_MAP) {
    xt_map *map = (xt_map *)object;
    xt_value clone = xt_map_ctor(0, NULL);
    xt_clone_remember(context, value, clone);
    for (uint32_t i = 0; i < map->count; i++) {
      xt_value args[2] = {xt_clone_value(context, map->keys[i]),
                          xt_clone_value(context, map->values[i])};
      xt_call_method(clone, xt_string_from_cstr("set"), 2, args);
    }
    return clone;
  }

  if (object->header.kind == XT_OBJECT_KIND_SET) {
    xt_set_object *set = (xt_set_object *)object;
    xt_value clone = xt_set_ctor(0, NULL);
    xt_clone_remember(context, value, clone);
    for (uint32_t i = 0; i < set->count; i++) {
      xt_value args[1] = {xt_clone_value(context, set->values[i])};
      xt_call_method(clone, xt_string_from_cstr("add"), 1, args);
    }
    return clone;
  }

  if (object->header.kind == XT_OBJECT_KIND_DATE) {
    xt_value time = xt_call_method(value, xt_string_from_cstr("getTime"), 0, NULL);
    xt_value clone = xt_date_ctor(1, &time);
    xt_clone_remember(context, value, clone);
    return clone;
  }

  if (object->header.kind == XT_OBJECT_KIND_REGEXP) {
    xt_value args[2] = {xt_get(value, xt_string_from_cstr("source")),
                        xt_get(value, xt_string_from_cstr("flags"))};
    xt_value clone = xt_regexp_ctor(2, args);
    xt_clone_remember(context, value, clone);
    return clone;
  }

  if (object->header.kind == XT_OBJECT_KIND_PROMISE) xt_clone_fail("Promise");
  if (object->header.kind == XT_OBJECT_KIND_GENERATOR) xt_clone_fail("generator");
  if (object->header.kind == XT_OBJECT_KIND_ITERATOR) xt_clone_fail("iterator");
  if (object->header.kind == XT_OBJECT_KIND_ASYNC) xt_clone_fail("async function");

  /* Plain objects and Errors are cloned as data: own enumerable string keys,
   * no prototype. That is what the platform algorithm does for ordinary
   * objects and class instances. */
  if (object->header.kind != XT_OBJECT_KIND_OBJECT && object->header.kind != XT_OBJECT_KIND_ERROR) {
    xt_clone_fail("value");
  }
  xt_value clone = xt_object_new();
  xt_clone_remember(context, value, clone);
  xt_value keys = xt_object_keys(value);
  xt_array *keyArray = xt_as_array(keys);
  if (!keyArray) return clone;
  for (uint32_t i = 0; i < keyArray->length; i++) {
    xt_value key = keyArray->items[i];
    xt_object_set(clone, key, xt_clone_value(context, xt_object_get(value, key)));
  }
  return clone;
}

xt_value xt_structured_clone(int32_t argc, xt_value *argv) {
  xt_clone_context context = {NULL, 0, 0};
  xt_value clone = xt_clone_value(&context, xt_arg_at(argc, argv, 0));
  free(context.entries);
  return clone;
}
