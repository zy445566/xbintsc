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
        if (extra != XT_UNDEFINED) {
          xt_value value = xt_object_get(extra, key);
          if (value != XT_UNDEFINED) return value;
        }
        /* Fall back to first-class built-in methods (`arr.map`). */
        return xt_method_value(target, key);
      }
      return xt_array_get(target, key);
    }
    /* Symbol-keyed properties live in the side object too. */
    if (xt_is_symbol(key)) {
      xt_value extra = xt_array_extra((xt_array *)XT_GET_PTR(target), 0);
      if (extra != XT_UNDEFINED) {
        xt_value value = xt_object_get(extra, key);
        if (value != XT_UNDEFINED) return value;
      }
      return xt_method_value(target, key);
    }
    return xt_array_get(target, key);
  }
  if (XT_IS_FUNCTION(target)) {
    /* Functions expose own `name` / `length` (arity) properties. */
    if (XT_IS_STRING(key)) {
      xt_string *k = xt_as_string(key);
      if (k->length == 6 && memcmp(k->data, "length", 6) == 0) {
        int32_t arity = ((xt_function *)XT_GET_PTR(target))->arity;
        return xt_number(arity >= 0 ? (double)arity : 0.0);
      }
      if (k->length == 4 && memcmp(k->data, "name", 4) == 0) {
        xt_string *name = ((xt_function *)XT_GET_PTR(target))->name;
        return name ? XT_FROM_PTR(XT_TAG_STRING, name) : xt_string_from_cstr("");
      }
    }
    {
      xt_value value = xt_object_get(target, key);
      if (value != XT_UNDEFINED) return value;
      return xt_method_value(target, key);
    }
  }
  if (XT_IS_OBJECT(target)) {
    /* Map/Set expose `.size`; promises, dates and regexps have their own
     * getters implemented in the standard library. */
    xt_object *obj = (xt_object *)XT_GET_PTR(target);
    if (obj->header.kind == XT_OBJECT_KIND_SYMBOL) {
      if (XT_IS_STRING(key)) {
        xt_string *k = xt_as_string(key);
        if (k->length == 11 && memcmp(k->data, "description", 11) == 0) {
          return xt_symbol_description(target);
        }
      }
      return xt_method_value(target, key);
    }
    if (obj->header.kind == XT_OBJECT_KIND_MAP || obj->header.kind == XT_OBJECT_KIND_SET) {
      if (XT_IS_STRING(key)) {
        xt_string *k = xt_as_string(key);
        if (k->length == 4 && memcmp(k->data, "size", 4) == 0) {
          int32_t size = obj->header.kind == XT_OBJECT_KIND_MAP ? xt_map_size(target) : xt_set_size(target);
          return xt_number((double)size);
        }
      }
    }
    if (obj->header.kind == XT_OBJECT_KIND_REGEXP) {
      xt_value regexpValue = xt_regexp_get_property(target, key);
      if (regexpValue != XT_UNDEFINED) return regexpValue;
    }
    if (obj->header.kind == XT_OBJECT_KIND_ERROR && XT_IS_STRING(key)) {
      xt_string *k = xt_as_string(key);
      if (k->length == 5 && memcmp(k->data, "stack", 5) == 0) {
        xt_value own = xt_object_get(target, key);
        return own != XT_UNDEFINED ? own : xt_error_to_string(target);
      }
    }
    {
      xt_value value = xt_object_get(target, key);
      if (value != XT_UNDEFINED) return value;
      return xt_method_value(target, key);
    }
  }
  if (XT_IS_STRING(target)) {
    /* Property access on a string: `.length`, numeric indexing and built-in
     * methods. */
    xt_string *s = xt_as_string(target);
    if (XT_IS_STRING(key)) {
      xt_string *k = xt_as_string(key);
      if (k->length == 6 && memcmp(k->data, "length", 6) == 0) return xt_number((double)s->length);
      uint32_t index = 0;
      if (xt_key_is_array_index(k, &index) && index < s->length) {
        return xt_string_new(s->data + index, 1);
      }
      return xt_method_value(target, key);
    }
    if (XT_IS_NUMBER(key)) {
      int32_t index = xt_to_int32(key);
      if (index >= 0 && (uint32_t)index < s->length) return xt_string_new(s->data + index, 1);
    }
    return XT_UNDEFINED;
  }
  if (XT_IS_STRING(key) &&
      (XT_IS_NUMBER(target) || XT_IS_BIGINT(target) || target == XT_TRUE || target == XT_FALSE)) {
    return xt_method_value(target, key);
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
      return xt_array_set(target, key, value);
    }
    if (xt_is_symbol(key)) {
      return xt_object_set(xt_array_extra((xt_array *)XT_GET_PTR(target), 1), key, value);
    }
    return xt_array_set(target, key, value);
  }
  if (XT_IS_FUNCTION(target)) return xt_object_set(target, key, value);
  if (XT_IS_OBJECT(target)) return xt_object_set(target, key, value);
  return value;
}
