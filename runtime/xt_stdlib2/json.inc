/* -- JSON ----------------------------------------------------------------- */
/* The parser and the serializer live in their own translation-unit fragments so
 * each stays readable; `xt_stdlib2.c` includes this file. */
#include "json_parse.inc"
#include "json_stringify.inc"

/* The `JSON` namespace as a first-class value: `const stringify = JSON.stringify`.
 * Call sites lower straight to `xt_json_parse` / `xt_json_stringify`; this
 * object exists for programs that read the namespace or destructure it. */
static xt_value xt_json_bridge(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  const char *method = xt_string_data(xt_closure_env(env, 0));
  if (method && strcmp(method, "parse") == 0) return xt_json_parse(argc, argv);
  if (method && strcmp(method, "stringify") == 0) return xt_json_stringify(argc, argv);
  return XT_UNDEFINED;
}

xt_value xt_json_object(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  static xt_value object = 0;
  if (object != 0) return object;
  object = xt_object_new();
  xt_gc_add_root(&object);
  static const char *const methods[] = {"parse", "stringify"};
  for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
    xt_value name = xt_string_from_cstr(methods[i]);
    xt_value closure = xt_closure_new((void *)xt_json_bridge, 1, &name);
    xt_function_set_metadata(closure, name, methods[i][0] == 'p' ? 1 : 1);
    xt_object_set(object, name, closure);
  }
  return object;
}
