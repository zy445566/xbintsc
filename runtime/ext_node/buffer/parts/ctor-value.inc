/* -- first-class constructor ---------------------------------------------- */

/*
 * `Buffer` is a Node global *and* a constructor value: libraries read
 * `Buffer[Symbol.species]`, pass `Buffer` to `instanceof`, or keep a reference
 * (`const FastBuffer = Buffer[Symbol.species]`). The code generator handles a
 * bare `Buffer` reference through the extension's `globalValues` hook, which
 * calls this getter.
 *
 * The value is a cached closure that runs `xt_buffer_ctor`, with the Buffer
 * statics installed as own properties so `const B = Buffer; B.from(...)` keeps
 * working, and `Buffer[Symbol.species]` answering the constructor itself (the
 * runtime has a single Buffer shape; Node's FastBuffer is that shape there).
 */

xt_value xt_buffer_global_value(int32_t argc, xt_value *argv);

static xt_value xt_buffer_value_invoke(xt_value thisValue, xt_value env, int32_t argc,
                                       xt_value *argv) {
  (void)thisValue;
  (void)env;
  return xt_buffer_ctor(argc, argv);
}

static xt_value xt_buffer_value_static_invoke(xt_value thisValue, xt_value env, int32_t argc,
                                              xt_value *argv) {
  (void)thisValue;
  return xt_buffer_static(xt_closure_env(env, 0), argc, argv);
}

static const char *const XT_BUFFER_VALUE_STATICS[] = {
    "from", "of", "alloc", "allocUnsafe", "allocUnsafeSlow", "isBuffer",
    "isUtf8", "byteLength", "concat", "compare",
};

xt_value xt_buffer_global_value(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  static xt_value ctor = 0;
  xt_gc_add_root(&ctor);
  if (ctor != 0) return ctor;

  ctor = xt_closure_new((void *)xt_buffer_value_invoke, 0, NULL);
  xt_function *function = (xt_function *)XT_GET_PTR(ctor);
  function->builtin_name = "Buffer";
  xt_function_set_metadata(ctor, xt_string_from_cstr("Buffer"), 2);
  xt_function_set_prototype(ctor, xt_buffer_proto());

  for (size_t i = 0; i < sizeof(XT_BUFFER_VALUE_STATICS) / sizeof(XT_BUFFER_VALUE_STATICS[0]); i++) {
    xt_value name = xt_string_from_cstr(XT_BUFFER_VALUE_STATICS[i]);
    xt_value env[1] = {name};
    xt_value method = xt_closure_new((void *)xt_buffer_value_static_invoke, 1, env);
    xt_function_set_metadata(method, name, 1);
    xt_object_set(ctor, name, method);
  }
  xt_object_set(ctor, xt_symbol_well_known("species"), ctor);
  return ctor;
}
