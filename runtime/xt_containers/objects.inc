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

static xt_property *xt_object_find(xt_object *obj, xt_value key) {
  for (uint32_t i = 0; i < obj->count; i++) {
    if (xt_property_key_equals(obj->properties[i].key, key)) return &obj->properties[i];
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

/* Insert a new property for `key`, preserving JavaScript key order. Symbols
 * are not array indices and simply append in insertion order. */
static xt_property *xt_object_insert_property(xt_object *obj, xt_value key) {
  uint32_t index = 0;
  uint32_t position = obj->count;
  if (XT_IS_STRING(key) && xt_key_is_array_index(xt_as_string(key), &index)) {
    for (uint32_t i = 0; i < obj->count; i++) {
      uint32_t existing = 0;
      xt_value existingKey = obj->properties[i].key;
      if (!XT_IS_STRING(existingKey) ||
          !xt_key_is_array_index(xt_as_string(existingKey), &existing)) {
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

xt_property *xt_object_find_property(xt_object *obj, xt_value key) {
  return obj ? xt_object_find(obj, key) : NULL;
}

/* Normalise a property key to a string value or a symbol value. */
xt_value xt_to_property_key(xt_value key) {
  if (xt_is_symbol(key)) return key;
  return xt_to_string(key);
}

/* Two property keys are equal when they are the same symbol, or equal
 * strings. */
int xt_property_key_equals(xt_value a, xt_value b) {
  if (XT_IS_STRING(a) && XT_IS_STRING(b)) return xt_string_equals(xt_as_string(a), xt_as_string(b));
  return a == b;
}

static int xt_key_is_prototype(xt_value key) {
  if (!XT_IS_STRING(key)) return 0;
  xt_string *k = xt_as_string(key);
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
  if (obj->header.kind != XT_OBJECT_KIND_OBJECT && obj->header.kind != XT_OBJECT_KIND_ERROR)
    return XT_UNDEFINED;
  xt_value k = xt_to_property_key(key);
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
  if (obj->header.kind != XT_OBJECT_KIND_OBJECT && obj->header.kind != XT_OBJECT_KIND_ERROR)
    return newValue;
  if (obj->frozen) return newValue;
  xt_value k = xt_to_property_key(key);
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

static xt_property *xt_object_ensure_property(xt_object *obj, xt_value key) {
  xt_value propertyKey = xt_to_property_key(key);
  xt_property *prop = xt_object_find(obj, propertyKey);
  if (prop) return prop;
  return xt_object_insert_property(obj, propertyKey);
}

xt_value xt_object_define_getter(xt_value value, xt_value key, xt_value getter) {
  xt_object *obj = xt_as_object(value);
  if (!obj) return value;
  xt_object_ensure_property(obj, key)->getter = getter;
  return value;
}

xt_value xt_object_define_setter(xt_value value, xt_value key, xt_value setter) {
  xt_object *obj = xt_as_object(value);
  if (!obj) return value;
  xt_object_ensure_property(obj, key)->setter = setter;
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
  return xt_bool(xt_object_find(obj, xt_to_property_key(key)) != NULL);
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
  return xt_bool(xt_object_find(obj, xt_to_property_key(key)) != NULL);
}

/* `Object.getOwnPropertySymbols`: the symbol-keyed own properties, in
 * insertion order. String keys are skipped. */
xt_value xt_object_own_property_symbols(xt_value value) {
  xt_value result = xt_array_new(0, NULL);
  if (!XT_IS_OBJECT(value)) return result;
  xt_object *obj = (xt_object *)XT_GET_PTR(value);
  if (obj->header.kind != XT_OBJECT_KIND_OBJECT && obj->header.kind != XT_OBJECT_KIND_ERROR)
    return result;
  for (uint32_t i = 0; i < obj->count; i++) {
    if (xt_is_symbol(obj->properties[i].key)) xt_array_push(result, obj->properties[i].key);
  }
  return result;
}

xt_value xt_object_keys(xt_value value) {
  xt_value result = xt_array_new(0, NULL);
  if (XT_IS_OBJECT(value)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(value);
    for (uint32_t i = 0; i < obj->count; i++) {
      if (XT_IS_STRING(obj->properties[i].key)) xt_array_push(result, obj->properties[i].key);
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
        if (XT_IS_STRING(extra->properties[i].key)) xt_array_push(result, extra->properties[i].key);
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
