/* ------------------------------------------------------------------------- */
/* Generic member access                                                     */
/* ------------------------------------------------------------------------- */

/* Inherited lookup for values whose representation is not an `xt_object`
 * property table (Map/Set/Date/RegExp/Promise/Symbol). `__proto__` returns the
 * prototype itself instead of invoking Object.prototype's getter with the
 * wrong receiver. */
static xt_value xt_get_inherited(xt_value target, xt_value key) {
  xt_value proto = xt_object_get_prototype(target);
  if (proto == XT_UNDEFINED || proto == XT_NULL || proto == target) return XT_UNDEFINED;
  if (XT_IS_STRING(key)) {
    xt_string *k = xt_as_string(key);
    if (k->length == 9 && memcmp(xt_string_bytes(k), "__proto__", 9) == 0) return proto;
  }
  return xt_get(proto, key);
}

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
  memcpy(keybuf, xt_string_bytes(k), n);
  keybuf[n] = 0;
  char message[256];
  snprintf(message, sizeof(message),
           "TypeError: Cannot read properties of %s (reading '%s')", base, keybuf);
  xt_throw(xt_error_from_message(message));
}

/* -- Array.prototype[Symbol.iterator] -------------------------------------- */
/*
 * `for...of` and spread use the internal index protocol, but libraries also
 * call the iterator method explicitly (`arr[Symbol.iterator]()`, `Array.from`,
 * manual `.next()`). Build a small iterator object per call: `next` closes over
 * the array and a one-element box holding the current index, and the iterator
 * is itself iterable so it can be passed back to `for...of` / spread.
 */
static xt_value xt_array_iterator_next(xt_value thisValue, xt_value env, int32_t argc,
                                       xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_value array = xt_closure_env(env, 0);
  xt_value indexBox = xt_closure_env(env, 1);
  int32_t index = xt_to_int32(xt_get(indexBox, xt_number(0)));
  xt_array *items = xt_as_array(array);
  xt_value result = xt_object_new();
  if (!items || index < 0 || (uint32_t)index >= items->length) {
    xt_object_set(result, xt_string_from_cstr("value"), XT_UNDEFINED);
    xt_object_set(result, xt_string_from_cstr("done"), XT_TRUE);
    return result;
  }
  xt_object_set(result, xt_string_from_cstr("value"), items->items[index]);
  xt_object_set(result, xt_string_from_cstr("done"), XT_FALSE);
  xt_set(indexBox, xt_number(0), xt_number(index + 1));
  return result;
}

static xt_value xt_iterator_return_this(xt_value thisValue, xt_value env, int32_t argc,
                                        xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return thisValue;
}

static xt_value xt_array_iterator(xt_value thisValue, xt_value env, int32_t argc,
                                  xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  if (!XT_IS_ARRAY(thisValue)) {
    xt_value message =
        xt_string_from_cstr("Array.prototype[Symbol.iterator] called on a non-array");
    xt_throw(xt_type_error_ctor(1, &message));
  }
  xt_value indexBox = xt_array_new(0, NULL);
  xt_array_push(indexBox, xt_number(0));
  xt_value captured[2] = {thisValue, indexBox};
  xt_value iterator = xt_object_new();
  xt_object_set(iterator, xt_string_from_cstr("next"),
                xt_closure_new((void *)xt_array_iterator_next, 2, captured));
  xt_object_set(iterator, xt_symbol_well_known("iterator"),
                xt_closure_new((void *)xt_iterator_return_this, 0, NULL));
  return iterator;
}

/* The method value is stable (`arr[Symbol.iterator] === arr[Symbol.iterator]`),
 * so cache it and root the cache for the collector. */
static xt_value xt_array_iterator_method_value = XT_UNDEFINED;

static xt_value xt_array_iterator_method(void) {
  if (!XT_IS_FUNCTION(xt_array_iterator_method_value)) {
    xt_array_iterator_method_value = xt_closure_new((void *)xt_array_iterator, 0, NULL);
    xt_gc_add_root(&xt_array_iterator_method_value);
  }
  return xt_array_iterator_method_value;
}

xt_value xt_get(xt_value target, xt_value key) {
  if (target == XT_UNDEFINED || target == XT_NULL) xt_throw_read_of_nullish(target, key);
  if (XT_IS_ARRAY(target)) {
    /* `arr.length` must read the array's length, never an element. */
    if (XT_IS_STRING(key)) {
      xt_string *k = xt_as_string(key);
      if (k->length == 6 && memcmp(xt_string_bytes(k), "length", 6) == 0)
        return xt_number((double)((xt_array *)XT_GET_PTR(target))->length);
      /* Named (non-index) properties such as `raw`/`index`/`input`. */
      uint32_t ignored = 0;
      if (!xt_key_is_array_index(k, &ignored)) {
        xt_value extra = xt_array_extra((xt_array *)XT_GET_PTR(target), 0);
        if (extra != XT_UNDEFINED) {
          xt_value value = xt_object_get(extra, key);
          if (value != XT_UNDEFINED) return value;
        }
        /* Fall back to first-class built-in methods (`arr.map`) and then to
         * the array's [[Prototype]] (`arr.hasOwnProperty`). */
        xt_value method = xt_method_value(target, key);
        if (method != XT_UNDEFINED) return method;
        xt_value proto = ((xt_array *)XT_GET_PTR(target))->prototype;
        if (proto != XT_UNDEFINED && proto != XT_NULL) {
          if (k->length == 9 && memcmp(xt_string_bytes(k), "__proto__", 9) == 0) return proto;
          return xt_get(proto, key);
        }
        return XT_UNDEFINED;
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
      /* Array.prototype[Symbol.iterator] is the fallback, after any own
       * symbol property the program assigned. */
      if (key == xt_symbol_well_known("iterator")) return xt_array_iterator_method();
      xt_value method = xt_method_value(target, key);
      if (method != XT_UNDEFINED) return method;
      xt_value proto = ((xt_array *)XT_GET_PTR(target))->prototype;
      if (proto != XT_UNDEFINED && proto != XT_NULL) return xt_get(proto, key);
      return XT_UNDEFINED;
    }
    return xt_array_get(target, key);
  }
  if (XT_IS_FUNCTION(target)) {
    /* Functions expose own `name` / `length` (arity) properties. */
    if (XT_IS_STRING(key)) {
      xt_string *k = xt_as_string(key);
      if (k->length == 6 && memcmp(xt_string_bytes(k), "length", 6) == 0) {
        int32_t arity = ((xt_function *)XT_GET_PTR(target))->arity;
        return xt_number(arity >= 0 ? (double)arity : 0.0);
      }
      if (k->length == 4 && memcmp(xt_string_bytes(k), "name", 4) == 0) {
        xt_string *name = ((xt_function *)XT_GET_PTR(target))->name;
        return name ? XT_FROM_PTR(XT_TAG_STRING, name) : xt_string_from_cstr("");
      }
    }
    {
      xt_value value = xt_object_get(target, key);
      if (value != XT_UNDEFINED) return value;
      /* `const A = Array; A.isArray`, `const S = Symbol; S.iterator`, ... */
      value = xt_builtin_static_value(target, key);
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
        if (k->length == 11 && memcmp(xt_string_bytes(k), "description", 11) == 0) {
          return xt_symbol_description(target);
        }
      }
      xt_value method = xt_method_value(target, key);
      if (method != XT_UNDEFINED) return method;
      return xt_get_inherited(target, key);
    }
    if (obj->header.kind == XT_OBJECT_KIND_MAP || obj->header.kind == XT_OBJECT_KIND_SET) {
      if (XT_IS_STRING(key)) {
        xt_string *k = xt_as_string(key);
        if (k->length == 4 && memcmp(xt_string_bytes(k), "size", 4) == 0) {
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
      if (k->length == 5 && memcmp(xt_string_bytes(k), "stack", 5) == 0) {
        xt_value captured = xt_error_stack(target);
        if (captured != XT_UNDEFINED) return captured;
        return xt_error_to_string(target);
      }
    }
    {
      /* Typed arrays keep their elements in a shared ArrayBuffer: serve
       * `.buffer` / element reads from it instead of the property bag. */
      int typed = xt_typed_array_kind_of(target);
      if (typed >= 0) {
        if (XT_IS_STRING(key)) {
          xt_string *k = xt_as_string(key);
          if (k->length == 6 && memcmp(xt_string_bytes(k), "buffer", 6) == 0) {
            return xt_typed_array_buffer_of(target);
          }
          if (k->length == 10 && memcmp(xt_string_bytes(k), "byteOffset", 10) == 0) {
            return xt_number((double)xt_typed_array_byte_offset_of(target));
          }
          uint32_t index = 0;
          if (xt_key_is_array_index(k, &index)) return xt_typed_array_read(target, index);
        } else if (XT_IS_NUMBER(key)) {
          int32_t index = xt_to_int32(key);
          if (index >= 0) return xt_typed_array_read(target, (uint32_t)index);
        }
      }
      xt_value value = xt_object_get(target, key);
      if (value != XT_UNDEFINED) return value;
      xt_value method = xt_method_value(target, key);
      if (method != XT_UNDEFINED) return method;
      if (obj->header.kind != XT_OBJECT_KIND_OBJECT && obj->header.kind != XT_OBJECT_KIND_ERROR) {
        return xt_get_inherited(target, key);
      }
      return XT_UNDEFINED;
    }
  }
  if (XT_IS_STRING(target)) {
    /* Property access on a string: `.length`, numeric indexing and built-in
     * methods. Indices are UTF-16 code units, so `s[0]` of a Chinese string is
     * the whole character and an emoji index into its surrogate pair. */
    xt_string *s = xt_as_string(target);
    uint32_t units = xt_string_utf16_length(s);
    if (XT_IS_STRING(key)) {
      xt_string *k = xt_as_string(key);
      if (k->length == 6 && memcmp(xt_string_bytes(k), "length", 6) == 0) return xt_number((double)units);
      uint32_t index = 0;
      if (xt_key_is_array_index(k, &index) && index < units) return xt_string_unit_value(s, (int32_t)index);
      return xt_method_value(target, key);
    }
    if (XT_IS_NUMBER(key)) {
      int32_t index = xt_to_int32(key);
      if (index >= 0 && (uint32_t)index < units) return xt_string_unit_value(s, index);
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
      if (k->length == 6 && memcmp(xt_string_bytes(k), "length", 6) == 0) {
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
  if (XT_IS_OBJECT(target)) {
    /* Writes to a typed-array element are coerced to its element type. */
    int typed = xt_typed_array_kind_of(target);
    if (typed >= 0) {
      if (XT_IS_STRING(key)) {
        xt_string *k = xt_as_string(key);
        if ((k->length == 6 && memcmp(xt_string_bytes(k), "length", 6) == 0) ||
            (k->length == 10 && memcmp(xt_string_bytes(k), "byteLength", 10) == 0) ||
            (k->length == 10 && memcmp(xt_string_bytes(k), "byteOffset", 10) == 0)) {
          xt_throw(xt_error_from_message("TypeError: Cannot assign to read only property"));
          return value;
        }
      }
      uint32_t index = 0;
      int isIndex = 0;
      if (XT_IS_NUMBER(key)) {
        int32_t numeric = xt_to_int32(key);
        if (numeric >= 0) {
          index = (uint32_t)numeric;
          isIndex = 1;
        }
      } else if (XT_IS_STRING(key)) {
        isIndex = xt_key_is_array_index(xt_as_string(key), &index);
      }
      if (isIndex) {
        double length = xt_to_number(xt_object_get_cstr(target, "length"));
        if (index >= (uint32_t)length) return value; /* out-of-bounds writes are ignored */
        xt_typed_array_write(target, index, value);
        return value;
      }
    }
    return xt_object_set(target, key, value);
  }
  return value;
}
