xt_value xt_ext_object_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "hasOwnProperty") == 0) return xt_object_has_own(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "toString") == 0) return xt_string_from_cstr("[object Object]");
  if (strcmp(method, "valueOf") == 0) return target;
  *handled = 0;
  return XT_UNDEFINED;
}

/* -- Object statics ------------------------------------------------------- */

/* `Object.getOwnPropertyNames` differs from `Object.keys` only in that it also
 * reports the non-enumerable `length` of arrays and strings. */
static xt_value xt_object_own_property_names(xt_value value) {
  xt_value names = xt_object_keys(value);
  if (XT_IS_ARRAY(value) || XT_IS_STRING(value)) xt_array_push(names, xt_string_from_cstr("length"));
  return names;
}

/* `Object.groupBy(items, fn)`: bucket each element by the key `fn` returns. */
static xt_value xt_object_group_by(xt_value value, xt_value fn) {
  xt_value result = xt_object_new();
  xt_array *array = xt_as_array(value);
  if (!array) return result;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value args[2] = {array->items[i], xt_number((double)i)};
    xt_value keyString = xt_to_string(xt_closure_call(fn, 2, args));
    xt_value group = xt_object_get(result, keyString);
    if (!XT_IS_ARRAY(group)) {
      group = xt_array_new(0, NULL);
      xt_object_set(result, keyString, group);
    }
    xt_array_push(group, array->items[i]);
  }
  return result;
}

/* -- property descriptors ------------------------------------------------- */

/* `attr` defaults for `defineProperty` are all false; literal properties are
 * all true. Store the bits per key under a symbol so the flag table stays out
 * of Object.keys / JSON. Writable/enumerable are reported truthfully, but the
 * runtime does not enforce them on later writes or enumeration yet. */
static xt_value xt_object_descriptor_flags_key_value = XT_UNDEFINED;

static xt_value xt_object_descriptor_flags_key(void) {
  if (!xt_is_symbol(xt_object_descriptor_flags_key_value)) {
    xt_object_descriptor_flags_key_value =
        xt_symbol_new(xt_string_from_cstr("__propertyDescriptorFlags"));
    xt_gc_add_root(&xt_object_descriptor_flags_key_value);
  }
  return xt_object_descriptor_flags_key_value;
}

static xt_value xt_object_descriptor_flags(xt_value target) {
  if (!XT_IS_OBJECT(target)) return XT_UNDEFINED;
  xt_value flags = xt_object_get(target, xt_object_descriptor_flags_key());
  if (!XT_IS_OBJECT(flags)) {
    flags = xt_object_new();
    xt_object_set(target, xt_object_descriptor_flags_key(), flags);
  }
  return flags;
}

static int xt_object_descriptor_mask(xt_value target, xt_value key) {
  if (!XT_IS_OBJECT(target)) return 7;
  xt_value flags = xt_object_get(target, xt_object_descriptor_flags_key());
  if (!XT_IS_OBJECT(flags)) return 7;
  xt_value mask = xt_object_get(flags, key);
  return mask == XT_UNDEFINED ? 7 : xt_to_int32(mask);
}

static xt_value xt_object_data_descriptor(xt_value value, int enumerable, int configurable,
                                         int writable) {
  xt_value descriptor = xt_object_new();
  xt_object_set(descriptor, xt_string_from_cstr("value"), value);
  xt_object_set(descriptor, xt_string_from_cstr("writable"), xt_bool(writable));
  xt_object_set(descriptor, xt_string_from_cstr("enumerable"), xt_bool(enumerable));
  xt_object_set(descriptor, xt_string_from_cstr("configurable"), xt_bool(configurable));
  return descriptor;
}

static xt_value xt_object_get_own_property_descriptor(xt_value target, xt_value key) {
  if (XT_IS_ARRAY(target)) {
    if (XT_IS_STRING(key) && xt_as_string(key)->length == 6 &&
        memcmp(xt_string_bytes(xt_as_string(key)), "length", 6) == 0) {
      return xt_object_data_descriptor(xt_array_length(target), 0, 0, 1);
    }
    xt_value item = xt_get(target, key);
    if (item != XT_UNDEFINED) return xt_object_data_descriptor(item, 1, 1, 1);
    return XT_UNDEFINED;
  }
  if (!XT_IS_OBJECT(target)) return XT_UNDEFINED;
  xt_object *object = (xt_object *)XT_GET_PTR(target);
  if (object->header.kind != XT_OBJECT_KIND_OBJECT && object->header.kind != XT_OBJECT_KIND_ERROR) {
    return XT_UNDEFINED;
  }
  xt_property *property = xt_object_find_property(object, xt_to_property_key(key));
  if (!property) return XT_UNDEFINED;
  int mask = xt_object_descriptor_mask(target, key);
  if (property->getter != XT_UNDEFINED || property->setter != XT_UNDEFINED) {
    xt_value descriptor = xt_object_new();
    if (property->getter != XT_UNDEFINED) {
      xt_object_set(descriptor, xt_string_from_cstr("get"), property->getter);
    }
    if (property->setter != XT_UNDEFINED) {
      xt_object_set(descriptor, xt_string_from_cstr("set"), property->setter);
    }
    xt_object_set(descriptor, xt_string_from_cstr("enumerable"), xt_bool((mask & 2) != 0));
    xt_object_set(descriptor, xt_string_from_cstr("configurable"), xt_bool((mask & 4) != 0));
    return descriptor;
  }
  return xt_object_data_descriptor(property->value, (mask & 2) != 0, (mask & 4) != 0,
                                   (mask & 1) != 0);
}

static xt_value xt_object_define_property(xt_value target, xt_value key, xt_value descriptor) {
  if (!XT_IS_OBJECT(descriptor)) return target;
  int writable = xt_truthy(xt_object_get(descriptor, xt_string_from_cstr("writable")));
  int enumerable = xt_truthy(xt_object_get(descriptor, xt_string_from_cstr("enumerable")));
  int configurable = xt_truthy(xt_object_get(descriptor, xt_string_from_cstr("configurable")));
  int mask = (writable ? 1 : 0) | (enumerable ? 2 : 0) | (configurable ? 4 : 0);
  xt_value flags = xt_object_descriptor_flags(target);
  if (XT_IS_OBJECT(flags)) xt_object_set(flags, key, xt_number((double)mask));
  xt_value getter = xt_object_get(descriptor, xt_string_from_cstr("get"));
  xt_value setter = xt_object_get(descriptor, xt_string_from_cstr("set"));
  if (getter != XT_UNDEFINED || setter != XT_UNDEFINED) {
    if (getter != XT_UNDEFINED) xt_object_define_getter(target, key, getter);
    if (setter != XT_UNDEFINED) xt_object_define_setter(target, key, setter);
    return target;
  }
  if (xt_object_has_own(descriptor, xt_string_from_cstr("value"))) {
    xt_set(target, key, xt_object_get(descriptor, xt_string_from_cstr("value")));
  }
  return target;
}

static xt_value xt_object_get_own_property_descriptors(xt_value target) {
  xt_value result = xt_object_new();
  xt_value names = xt_object_own_property_names(target);
  xt_array *keys = xt_as_array(names);
  if (!keys) return result;
  for (uint32_t i = 0; i < keys->length; i++) {
    xt_value descriptor = xt_object_get_own_property_descriptor(target, keys->items[i]);
    if (descriptor != XT_UNDEFINED) xt_object_set(result, keys->items[i], descriptor);
  }
  return result;
}

xt_value xt_object_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "getOwnPropertyDescriptor") == 0) {
    return xt_object_get_own_property_descriptor(xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  }
  if (strcmp(fn, "getOwnPropertyDescriptors") == 0) {
    return xt_object_get_own_property_descriptors(xt_arg_at(argc, argv, 0));
  }
  if (strcmp(fn, "defineProperty") == 0) {
    return xt_object_define_property(xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1),
                                      xt_arg_at(argc, argv, 2));
  }
  if (strcmp(fn, "keys") == 0) return xt_object_keys(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "getOwnPropertyNames") == 0) return xt_object_own_property_names(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "getOwnPropertySymbols") == 0) return xt_object_own_property_symbols(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "groupBy") == 0) return xt_object_group_by(xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(fn, "values") == 0) return xt_object_values(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "entries") == 0) return xt_object_entries(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "assign") == 0) return xt_object_assign(argc, argv);
  if (strcmp(fn, "freeze") == 0) return xt_object_freeze(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "isFrozen") == 0) return xt_bool(xt_object_is_frozen(xt_arg_at(argc, argv, 0)));
  if (strcmp(fn, "fromEntries") == 0) return xt_object_from_entries(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "getPrototypeOf") == 0) return xt_object_get_prototype(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "setPrototypeOf") == 0) return xt_object_set_prototype(xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(fn, "hasOwn") == 0) return xt_object_has_own(xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(fn, "is") == 0) {
    xt_value a = xt_arg_at(argc, argv, 0);
    xt_value b = xt_arg_at(argc, argv, 1);
    if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) {
      double x = xt_to_double(a), y = xt_to_double(b);
      if (isnan(x) && isnan(y)) return XT_TRUE;
      if (x == 0 && y == 0) return xt_bool(signbit(x) == signbit(y));
      return xt_bool(x == y);
    }
    return xt_seq(a, b);
  }
  if (strcmp(fn, "create") == 0) {
    xt_value proto = xt_arg_at(argc, argv, 0);
    return xt_object_new_with_proto(proto == XT_NULL ? XT_NULL : proto);
  }
  return XT_UNDEFINED;
}
