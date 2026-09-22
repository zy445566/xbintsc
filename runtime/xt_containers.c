/*
 * xbintsc runtime — containers.
 *
 * Objects (linear property lists) and arrays (dynamic vectors), plus the
 * generic `xt_get`/`xt_set` member access used by the codegen and the boxing
 * helpers used for captured `let` bindings.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Objects                                                                   */
/* ------------------------------------------------------------------------- */

static void xt_object_reserve(xt_object *obj, uint32_t needed) {
  if (needed <= obj->capacity) return;
  uint32_t capacity = obj->capacity ? obj->capacity * 2 : 8;
  while (capacity < needed) capacity *= 2;
  obj->properties = (xt_property *)realloc(obj->properties, sizeof(xt_property) * capacity);
  if (!obj->properties) {
    fprintf(stderr, "xbintsc: out of memory growing object\n");
    abort();
  }
  obj->capacity = capacity;
}

xt_value xt_object_new(void) { return xt_object_new_with_proto(XT_UNDEFINED); }

xt_value xt_object_new_with_proto(xt_value proto) {
  xt_object *obj = (xt_object *)xt_alloc(sizeof(xt_object), XT_OBJECT_KIND_OBJECT);
  obj->count = 0;
  obj->capacity = 0;
  obj->properties = NULL;
  obj->prototype = proto;
  obj->frozen = 0;
  obj->is_array_base = 0;
  return XT_FROM_PTR(XT_TAG_OBJECT, obj);
}

xt_object *xt_as_object(xt_value value) {
  if (XT_IS_OBJECT(value)) return (xt_object *)XT_GET_PTR(value);
  if (XT_IS_FUNCTION(value)) {
    /* Functions carry their own property bag (used for static members). */
    xt_function *fn = (xt_function *)XT_GET_PTR(value);
    if (!fn->properties) fn->properties = (xt_object *)XT_GET_PTR(xt_object_new_with_proto(XT_UNDEFINED));
    return fn->properties;
  }
  return NULL;
}

static xt_property *xt_object_find(xt_object *obj, xt_string *key) {
  for (uint32_t i = 0; i < obj->count; i++) {
    if (xt_string_equals(obj->properties[i].key, key)) return &obj->properties[i];
  }
  return NULL;
}

/*
 * JavaScript enumerates own string keys in a canonical order: array-index
 * keys (the canonical decimal form of an integer in [0, 2^32 - 2]) first, in
 * ascending numeric order, then the remaining keys in insertion order. We keep
 * the physical property array in exactly that order so every consumer
 * (`Object.keys`, `for...in`, `JSON.stringify`, ...) is correct for free.
 */
static int xt_key_is_array_index(xt_string *key, uint32_t *out) {
  if (key->length == 0 || key->length > 10) return 0;
  if (key->data[0] == '0' && key->length != 1) return 0; /* "0" is canonical, "01" is not */
  uint64_t value = 0;
  for (uint32_t i = 0; i < key->length; i++) {
    char ch = key->data[i];
    if (ch < '0' || ch > '9') return 0;
    value = value * 10 + (uint64_t)(ch - '0');
  }
  if (value > 4294967294ULL) return 0; /* 2^32 - 2 is the largest array index */
  *out = (uint32_t)value;
  return 1;
}

/* Insert a new property for `key`, preserving JavaScript key order. */
static xt_property *xt_object_insert_property(xt_object *obj, xt_string *key) {
  uint32_t index = 0;
  uint32_t position = obj->count;
  if (xt_key_is_array_index(key, &index)) {
    for (uint32_t i = 0; i < obj->count; i++) {
      uint32_t existing = 0;
      if (!xt_key_is_array_index(obj->properties[i].key, &existing)) {
        position = i; /* first non-index key: the integer prefix ends here */
        break;
      }
      if (existing > index) {
        position = i;
        break;
      }
    }
  }
  xt_object_reserve(obj, obj->count + 1);
  for (uint32_t i = obj->count; i > position; i--) obj->properties[i] = obj->properties[i - 1];
  xt_property *prop = &obj->properties[position];
  prop->key = key;
  prop->value = XT_UNDEFINED;
  prop->getter = XT_UNDEFINED;
  prop->setter = XT_UNDEFINED;
  obj->count++;
  return prop;
}

xt_property *xt_object_find_property(xt_object *obj, xt_string *key) {
  return obj ? xt_object_find(obj, key) : NULL;
}

static int xt_key_is_prototype(xt_value key) {
  xt_string *k = xt_as_string(xt_to_string(key));
  return k && k->length == 9 && memcmp(k->data, "prototype", 9) == 0;
}

xt_value xt_object_get(xt_value value, xt_value key) {
  /* A function's `.prototype` lives in a dedicated field (set by
     `xt_function_set_prototype`), not in its property bag. Expose it as a
     normal property so `Class.prototype` returns the object instances use. */
  if (XT_IS_FUNCTION(value) && xt_key_is_prototype(key)) {
    return ((xt_function *)XT_GET_PTR(value))->prototype;
  }
  xt_object *obj = xt_as_object(value);
  if (!obj) return XT_UNDEFINED;
  /* Only plain objects (and function property bags) use the `xt_object`
     layout. Containers such as Map/Set/Date/RegExp/Promise/Array must not be
     interpreted as property tables, or property lookup reads past the end of
     their real representation. */
  if (obj->header.kind != XT_OBJECT_KIND_OBJECT) return XT_UNDEFINED;
  xt_value keyString = xt_to_string(key);
  xt_string *k = xt_as_string(keyString);
  /* Walk the prototype chain like JavaScript property lookup. */
  xt_object *cur = obj;
  while (cur) {
    xt_property *prop = xt_object_find(cur, k);
    if (prop) {
      /* Invoke accessors with the original receiver as `this`, so getters
         inherited from a prototype still see the instance. */
      if (prop->getter != XT_UNDEFINED && XT_IS_FUNCTION(prop->getter)) {
        return xt_call_with_this(prop->getter, value, 0, NULL);
      }
      if (prop->getter != XT_UNDEFINED) return XT_UNDEFINED;
      return prop->value;
    }
    if (!XT_IS_OBJECT(cur->prototype)) break;
    cur = (xt_object *)XT_GET_PTR(cur->prototype);
  }
  return XT_UNDEFINED;
}

xt_value xt_object_set(xt_value value, xt_value key, xt_value newValue) {
  if (XT_IS_FUNCTION(value) && xt_key_is_prototype(key)) {
    return xt_function_set_prototype(value, newValue);
  }
  xt_object *obj = xt_as_object(value);
  if (!obj) return newValue;
  if (obj->header.kind != XT_OBJECT_KIND_OBJECT) return newValue;
  if (obj->frozen) return newValue;
  xt_value keyString = xt_to_string(key);
  xt_string *k = xt_as_string(keyString);
  xt_property *prop = xt_object_find(obj, k);
  if (prop) {
    if (prop->setter != XT_UNDEFINED) {
      if (XT_IS_FUNCTION(prop->setter)) {
        xt_value argument = newValue;
        xt_call_with_this(prop->setter, value, 1, &argument);
      }
      return newValue;
    }
    if (prop->getter != XT_UNDEFINED) return newValue; /* accessor without setter */
    prop->value = newValue;
    return newValue;
  }
  /* No own property: an inherited accessor still intercepts the assignment
     (JavaScript assigns through prototype setters instead of shadowing). */
  xt_object *ancestor = XT_IS_OBJECT(obj->prototype) ? (xt_object *)XT_GET_PTR(obj->prototype) : NULL;
  while (ancestor) {
    xt_property *inherited = xt_object_find(ancestor, k);
    if (inherited) {
      if (inherited->setter != XT_UNDEFINED) {
        if (XT_IS_FUNCTION(inherited->setter)) {
          xt_value argument = newValue;
          xt_call_with_this(inherited->setter, value, 1, &argument);
        }
        return newValue;
      }
      if (inherited->getter != XT_UNDEFINED) return newValue; /* getter-only accessor */
      break; /* plain inherited data property: create an own shadowing property */
    }
    ancestor = XT_IS_OBJECT(ancestor->prototype) ? (xt_object *)XT_GET_PTR(ancestor->prototype) : NULL;
  }
  xt_property *inserted = xt_object_insert_property(obj, k);
  inserted->value = newValue;
  return newValue;
}

static xt_property *xt_object_ensure_property(xt_object *obj, xt_value keyString) {
  xt_property *prop = xt_object_find(obj, xt_as_string(keyString));
  if (prop) return prop;
  return xt_object_insert_property(obj, xt_as_string(keyString));
}

xt_value xt_object_define_getter(xt_value value, xt_value key, xt_value getter) {
  xt_object *obj = xt_as_object(value);
  if (!obj) return value;
  xt_object_ensure_property(obj, xt_to_string(key))->getter = getter;
  return value;
}

xt_value xt_object_define_setter(xt_value value, xt_value key, xt_value setter) {
  xt_object *obj = xt_as_object(value);
  if (!obj) return value;
  xt_object_ensure_property(obj, xt_to_string(key))->setter = setter;
  return value;
}

xt_value xt_object_get_prototype(xt_value value) {
  xt_object *obj = xt_as_object(value);
  if (obj) return obj->prototype;
  return XT_UNDEFINED;
}

xt_value xt_object_set_prototype(xt_value value, xt_value proto) {
  xt_object *obj = xt_as_object(value);
  if (obj && !obj->frozen) obj->prototype = proto;
  return value;
}

xt_value xt_object_freeze(xt_value value) {
  xt_object *obj = xt_as_object(value);
  if (obj) obj->frozen = 1;
  return value;
}

int xt_object_is_frozen(xt_value value) {
  xt_object *obj = xt_as_object(value);
  return obj ? obj->frozen : 0;
}

xt_value xt_object_has_own(xt_value value, xt_value key) {
  xt_object *obj = xt_as_object(value);
  if (!obj) return XT_FALSE;
  xt_value keyString = xt_to_string(key);
  return xt_bool(xt_object_find(obj, xt_as_string(keyString)) != NULL);
}

xt_value xt_object_from_entries(xt_value entries) {
  xt_value result = xt_object_new();
  xt_array *array = xt_as_array(entries);
  if (!array) return result;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_array *pair = xt_as_array(array->items[i]);
    if (!pair || pair->length < 2) continue;
    xt_object_set(result, pair->items[0], pair->items[1]);
  }
  return result;
}

xt_value xt_object_has(xt_value value, xt_value key) {
  if (!XT_IS_OBJECT(value)) return XT_FALSE;
  xt_object *obj = (xt_object *)XT_GET_PTR(value);
  xt_value keyString = xt_to_string(key);
  return xt_bool(xt_object_find(obj, xt_as_string(keyString)) != NULL);
}

xt_value xt_object_keys(xt_value value) {
  xt_value result = xt_array_new(0, NULL);
  if (XT_IS_OBJECT(value)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(value);
    for (uint32_t i = 0; i < obj->count; i++) {
      xt_array_push(result, XT_FROM_PTR(XT_TAG_STRING, obj->properties[i].key));
    }
    return result;
  }
  if (XT_IS_ARRAY(value)) {
    xt_array *array = (xt_array *)XT_GET_PTR(value);
    for (uint32_t i = 0; i < array->length; i++) {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%u", i);
      xt_array_push(result, xt_string_from_cstr(buffer));
    }
    if (XT_IS_OBJECT(array->extra)) {
      xt_object *extra = (xt_object *)XT_GET_PTR(array->extra);
      for (uint32_t i = 0; i < extra->count; i++) {
        xt_array_push(result, XT_FROM_PTR(XT_TAG_STRING, extra->properties[i].key));
      }
    }
    return result;
  }
  if (XT_IS_STRING(value)) {
    xt_string *s = xt_as_string(value);
    for (uint32_t i = 0; i < s->length; i++) {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%u", i);
      xt_array_push(result, xt_string_from_cstr(buffer));
    }
    return result;
  }
  return result;
}

/* Convenience used by codegen for `obj.prop`. */
xt_value xt_object_get_cstr(xt_value value, const char *key) {
  xt_value k = xt_string_from_cstr(key);
  return xt_object_get(value, k);
}

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
  /* Spreading a string/Map/Set iterates exactly like `for...of`. */
  int32_t n = xt_to_int32(xt_iter_length(source));
  for (int32_t i = 0; i < n; i++) {
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


/* ------------------------------------------------------------------------- */
/* Generic member access                                                     */
/* ------------------------------------------------------------------------- */

/*
 * Reading a property of `null` / `undefined` is a TypeError in JavaScript.
 * Short-circuiting `?.` guards forbid reaching this point for a nullish
 * receiver, so a non-optional access on a nullish base is a genuine bug in
 * the compiled program (e.g. `o?.a.b` where `o.a` is null).
 */
static void xt_throw_read_of_nullish(xt_value target, xt_value key) {
  const char *base = target == XT_NULL ? "null" : "undefined";
  char keybuf[128];
  xt_string *k = XT_IS_STRING(key) ? xt_as_string(key) : xt_as_string(xt_to_string(key));
  uint32_t n = k->length < sizeof(keybuf) - 1 ? k->length : (uint32_t)(sizeof(keybuf) - 1);
  memcpy(keybuf, k->data, n);
  keybuf[n] = 0;
  char message[256];
  snprintf(message, sizeof(message),
           "TypeError: Cannot read properties of %s (reading '%s')", base, keybuf);
  xt_throw(xt_string_from_cstr(message));
}

xt_value xt_get(xt_value target, xt_value key) {
  if (target == XT_UNDEFINED || target == XT_NULL) xt_throw_read_of_nullish(target, key);
  if (XT_IS_ARRAY(target)) {
    /* `arr.length` must read the array's length, never an element. */
    if (XT_IS_STRING(key)) {
      xt_string *k = xt_as_string(key);
      if (k->length == 6 && memcmp(k->data, "length", 6) == 0)
        return xt_number((double)((xt_array *)XT_GET_PTR(target))->length);
      /* Named (non-index) properties such as `raw`/`index`/`input`. */
      uint32_t ignored = 0;
      if (!xt_key_is_array_index(k, &ignored)) {
        xt_value extra = xt_array_extra((xt_array *)XT_GET_PTR(target), 0);
        if (extra == XT_UNDEFINED) return XT_UNDEFINED;
        return xt_object_get(extra, key);
      }
    }
    return xt_array_get(target, key);
  }
  if (XT_IS_FUNCTION(target)) return xt_object_get(target, key);
  if (XT_IS_OBJECT(target)) {
    /* Map/Set expose `.size`; promises, dates and regexps have their own
     * getters implemented in the standard library. */
    xt_object *obj = (xt_object *)XT_GET_PTR(target);
    if (obj->header.kind == XT_OBJECT_KIND_MAP || obj->header.kind == XT_OBJECT_KIND_SET) {
      if (XT_IS_STRING(key)) {
        xt_string *k = xt_as_string(key);
        if (k->length == 4 && memcmp(k->data, "size", 4) == 0) {
          int32_t size = obj->header.kind == XT_OBJECT_KIND_MAP ? xt_map_size(target) : xt_set_size(target);
          return xt_number((double)size);
        }
      }
    }
    if (obj->header.kind == XT_OBJECT_KIND_REGEXP) return xt_regexp_get_property(target, key);
    return xt_object_get(target, key);
  }
  if (XT_IS_STRING(target)) {
    /* Property access on a string: `.length` and numeric indexing. */
    xt_string *s = xt_as_string(target);
    if (XT_IS_STRING(key)) {
      xt_string *k = xt_as_string(key);
      if (k->length == 6 && memcmp(k->data, "length", 6) == 0) return xt_number((double)s->length);
    }
    int32_t index = xt_to_int32(key);
    if (index >= 0 && (uint32_t)index < s->length) return xt_string_new(s->data + index, 1);
    return XT_UNDEFINED;
  }
  return XT_UNDEFINED;
}

xt_value xt_set(xt_value target, xt_value key, xt_value value) {
  if (XT_IS_ARRAY(target)) {
    /* Assigning `arr.length` truncates or extends the array. */
    if (XT_IS_STRING(key)) {
      xt_string *k = xt_as_string(key);
      if (k->length == 6 && memcmp(k->data, "length", 6) == 0) {
        xt_array *array = (xt_array *)XT_GET_PTR(target);
        int32_t n = xt_to_int32(value);
        if (n < 0) n = 0;
        xt_array_reserve(array, (uint32_t)n);
        for (uint32_t j = array->length; j < (uint32_t)n; j++) array->items[j] = XT_UNDEFINED;
        array->length = (uint32_t)n;
        return value;
      }
      uint32_t ignored = 0;
      if (!xt_key_is_array_index(k, &ignored)) {
        return xt_object_set(xt_array_extra((xt_array *)XT_GET_PTR(target), 1), key, value);
      }
    }
    return xt_array_set(target, key, value);
  }
  if (XT_IS_FUNCTION(target)) return xt_object_set(target, key, value);
  if (XT_IS_OBJECT(target)) return xt_object_set(target, key, value);
  return value;
}

/* ------------------------------------------------------------------------- */
/* Boxes                                                                     */
/* ------------------------------------------------------------------------- */

xt_value xt_box_new(xt_value value) { return xt_array_new(1, &value); }

xt_value xt_box_get(xt_value box) { return xt_array_get(box, xt_number(0)); }

xt_value xt_box_set(xt_value box, xt_value value) { return xt_array_set(box, xt_number(0), value); }

xt_value xt_is_nullish(xt_value value) { return xt_bool(value == XT_UNDEFINED || value == XT_NULL); }

