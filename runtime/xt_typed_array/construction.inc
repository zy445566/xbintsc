/* xbintsc runtime - typed array constructors, prototypes and statics.
 * Included by `xt_typed_array.c` as part of a single translation unit. */

/* -- construction --------------------------------------------------------- */

static void xt_ta_define(xt_value proto, const char *name, void *fn) {
  xt_object_set(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

static xt_value xt_ta_proto(int kind) {
  static int registered = 0;
  if (!registered) {
    registered = 1;
    xt_gc_add_root_range(xt_ta_protos, XT_TA_KIND_COUNT);
  }
  if (kind < 0 || kind >= XT_TA_KIND_COUNT) return XT_UNDEFINED;
  if (xt_ta_protos[kind]) return xt_ta_protos[kind];
  xt_value proto = xt_object_new();
  xt_ta_protos[kind] = proto;
  xt_object_set(proto, xt_string_from_cstr("BYTES_PER_ELEMENT"), xt_number((double)xt_ta_bytes[kind]));
  xt_ta_define(proto, "fill", (void *)xt_ta_method_fill);
  xt_ta_define(proto, "set", (void *)xt_ta_method_set);
  xt_ta_define(proto, "slice", (void *)xt_ta_method_slice);
  xt_ta_define(proto, "subarray", (void *)xt_ta_method_slice);
  xt_ta_define(proto, "join", (void *)xt_ta_method_join);
  xt_ta_define(proto, "toString", (void *)xt_ta_method_to_string);
  xt_ta_define(proto, "toLocaleString", (void *)xt_ta_method_to_string);
  xt_ta_define(proto, "indexOf", (void *)xt_ta_method_index_of);
  xt_ta_define(proto, "lastIndexOf", (void *)xt_ta_method_last_index_of);
  xt_ta_define(proto, "includes", (void *)xt_ta_method_includes);
  xt_ta_define(proto, "forEach", (void *)xt_ta_method_for_each);
  xt_ta_define(proto, "map", (void *)xt_ta_method_map);
  xt_ta_define(proto, "filter", (void *)xt_ta_method_filter);
  xt_ta_define(proto, "every", (void *)xt_ta_method_every);
  xt_ta_define(proto, "some", (void *)xt_ta_method_some);
  xt_ta_define(proto, "find", (void *)xt_ta_method_find);
  xt_ta_define(proto, "findIndex", (void *)xt_ta_method_find_index);
  xt_ta_define(proto, "reduce", (void *)xt_ta_method_reduce);
  xt_ta_define(proto, "reverse", (void *)xt_ta_method_reverse);
  xt_ta_define(proto, "sort", (void *)xt_ta_method_sort);
  xt_ta_define(proto, "copyWithin", (void *)xt_ta_method_copy_within);
  xt_ta_define(proto, "at", (void *)xt_ta_method_at);
  xt_ta_define(proto, "keys", (void *)xt_ta_method_keys);
  xt_ta_define(proto, "values", (void *)xt_ta_method_values);
  xt_ta_define(proto, "entries", (void *)xt_ta_method_entries);
  return proto;
}

static xt_value xt_ta_new(int kind, uint32_t length) {
  xt_value array = xt_object_new_with_proto(xt_ta_proto(kind));
  xt_object_set(array, xt_string_from_cstr("length"), xt_number((double)length));
  xt_object_set(array, xt_string_from_cstr("byteLength"), xt_number((double)length * xt_ta_bytes[kind]));
  xt_object_set(array, xt_string_from_cstr("byteOffset"), xt_number(0));
  for (uint32_t i = 0; i < length; i++) xt_ta_store(array, kind, i, 0);
  return array;
}

/* Length of any value a typed array can be built from, or 0 when it is not
 * array-like (strings/iterables are intentionally not treated as element
 * sources yet). */
static int xt_ta_length_of(xt_value value, uint32_t *out) {
  if (XT_IS_ARRAY(value)) {
    *out = (uint32_t)xt_to_number(xt_array_length(value));
    return 1;
  }
  if (XT_IS_STRING(value)) {
    *out = xt_as_string(value)->length;
    return 1;
  }
  if (xt_typed_array_kind_of(value) >= 0) {
    *out = xt_ta_length(value);
    return 1;
  }
  if (XT_IS_OBJECT(value)) {
    xt_value length = xt_object_get_cstr(value, "length");
    if (length != XT_UNDEFINED) {
      double size = xt_to_number(length);
      *out = size > 0 ? (uint32_t)size : 0;
      return 1;
    }
  }
  return 0;
}

static xt_value xt_ta_from_value(int kind, xt_value value, xt_value mapFn, xt_value thisArg, int haveMap) {
  uint32_t length = 0;
  if (!xt_ta_length_of(value, &length)) length = 0;
  xt_value array = xt_ta_new(kind, length);
  for (uint32_t i = 0; i < length; i++) {
    xt_value item = xt_get(value, xt_number((double)i));
    if (haveMap) {
      xt_value args[3] = {item, xt_number((double)i), array};
      item = xt_call_with_this(mapFn, thisArg, 3, args);
    }
    xt_ta_store(array, kind, i, xt_to_number(item));
  }
  return array;
}

static xt_value xt_ta_ctor_impl(int kind, int32_t argc, xt_value *argv) {
  if (argc == 0) return xt_ta_new(kind, 0);
  xt_value argument = argv[0];
  /* Objects (arrays / typed arrays / array-likes / iterables) are copied
   * element-wise; every other argument is a length via `ToIndex`. */
  if (XT_IS_ARRAY(argument) || XT_IS_OBJECT(argument)) {
    return xt_ta_from_value(kind, argument, XT_UNDEFINED, XT_UNDEFINED, 0);
  }
  double size = xt_to_number(argument);
  return xt_ta_new(kind, size > 0 ? (uint32_t)trunc(size) : 0);
}

static xt_value xt_ta_static_impl(int kind, xt_value name, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(name);
  if (!method) return XT_UNDEFINED;
  if (strcmp(method, "from") == 0) {
    int haveMap = argc > 1 && XT_IS_FUNCTION(argv[1]);
    return xt_ta_from_value(kind, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), xt_arg_at(argc, argv, 2), haveMap);
  }
  if (strcmp(method, "of") == 0) {
    xt_value array = xt_ta_new(kind, (uint32_t)argc);
    for (int32_t i = 0; i < argc; i++) xt_ta_store(array, kind, (uint32_t)i, xt_to_number(argv[i]));
    return array;
  }
  return XT_UNDEFINED;
}

#define XT_TA_CTOR(suffix, kind) \
  xt_value xt_ta_##suffix##_ctor(int32_t argc, xt_value *argv) { return xt_ta_ctor_impl(kind, argc, argv); } \
  xt_value xt_ta_##suffix##_static(xt_value name, int32_t argc, xt_value *argv) { \
    return xt_ta_static_impl(kind, name, argc, argv); \
  }

XT_TA_CTOR(u8, XT_TA_U8)
XT_TA_CTOR(i8, XT_TA_I8)
XT_TA_CTOR(u8c, XT_TA_U8C)
XT_TA_CTOR(u16, XT_TA_U16)
XT_TA_CTOR(i16, XT_TA_I16)
XT_TA_CTOR(u32, XT_TA_U32)
XT_TA_CTOR(i32, XT_TA_I32)
XT_TA_CTOR(f32, XT_TA_F32)
XT_TA_CTOR(f64, XT_TA_F64)

