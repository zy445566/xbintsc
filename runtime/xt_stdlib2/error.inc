/* ------------------------------------------------------------------------- */
/* Error family: prototype chain, first-class constructors and formatting.    */

/* Every constructor in the Error family shares one implementation; the name
 * is what distinguishes them. Prototypes chain `TypeError.prototype` (etc.)
 * to `Error.prototype`, so `instanceof Error` succeeds for every member. */
static const char *const XT_ERROR_NAMES[] = {
    "Error",     "TypeError", "RangeError",  "SyntaxError",
    "ReferenceError", "EvalError", "URIError", "AggregateError",
};

static int xt_error_is_family(const char *name) {
  for (size_t i = 0; i < sizeof(XT_ERROR_NAMES) / sizeof(XT_ERROR_NAMES[0]); i++) {
    if (strcmp(name, XT_ERROR_NAMES[i]) == 0) return 1;
  }
  return 0;
}

static xt_value xt_error_root_proto = XT_UNDEFINED;

static xt_value xt_error_proto_for(const char *name) {
  xt_gc_add_root(&xt_error_root_proto);
  if (!XT_IS_OBJECT(xt_error_root_proto)) {
    xt_error_root_proto = xt_object_new();
    xt_object_set(xt_error_root_proto, xt_string_from_cstr("name"),
                  xt_string_from_cstr("Error"));
    xt_object_set(xt_error_root_proto, xt_string_from_cstr("message"),
                  xt_string_from_cstr(""));
  }
  if (strcmp(name, "Error") == 0) return xt_error_root_proto;
  xt_value key = xt_string_from_cstr(name);
  xt_value registry = xt_object_get(xt_error_root_proto, xt_string_from_cstr("__children"));
  if (!XT_IS_OBJECT(registry)) {
    registry = xt_object_new();
    xt_object_set(xt_error_root_proto, xt_string_from_cstr("__children"), registry);
  }
  xt_value existing = xt_object_get(registry, key);
  if (XT_IS_OBJECT(existing)) return existing;
  xt_value proto = xt_object_new();
  xt_object_set_prototype(proto, xt_error_root_proto);
  xt_object_set(proto, xt_string_from_cstr("name"), key);
  xt_object_set(proto, xt_string_from_cstr("message"), xt_string_from_cstr(""));
  xt_object_set(registry, key, proto);
  return proto;
}

xt_value xt_error_prototype(xt_value nameValue) {
  const char *name = XT_IS_STRING(nameValue) ? xt_string_data(nameValue) : "Error";
  if (!xt_error_is_family(name)) name = "Error";
  return xt_error_proto_for(name);
}

/* Capture `Error.stack` into the object's private field (not a property, so it
 * stays out of Object.keys/JSON.stringify, like V8's own non-enumerable stack). */
static void xt_error_capture_stack(xt_value error, const char *name, xt_value message) {
  if (!XT_IS_OBJECT(error)) return;
  xt_string *text = xt_as_string(xt_to_string(message));
  xt_value stack = xt_stack_capture(name, text ? xt_string_bytes(text) : "");
  ((xt_object *)XT_GET_PTR(error))->error_stack = stack;
}

static xt_value xt_make_error(const char *name, int32_t argc, xt_value *argv) {
  if (!xt_error_is_family(name)) name = "Error";
  xt_value error = xt_object_new();
  ((xt_object *)XT_GET_PTR(error))->header.kind = XT_OBJECT_KIND_ERROR;
  xt_object_set_prototype(error, xt_error_proto_for(name));
  xt_value message;
  if (strcmp(name, "AggregateError") == 0) {
    xt_value errors = xt_arg_at(argc, argv, 0);
    xt_object_set(error, xt_string_from_cstr("errors"),
                  errors == XT_UNDEFINED ? xt_array_new(0, NULL) : errors);
    message = xt_arg_at(argc, argv, 1);
  } else {
    message = xt_arg_at(argc, argv, 0);
  }
  xt_value messageValue = message == XT_UNDEFINED ? xt_string_from_cstr("") : message;
  xt_object_set(error, xt_string_from_cstr("name"), xt_string_from_cstr(name));
  xt_object_set(error, xt_string_from_cstr("message"), messageValue);
  xt_error_capture_stack(error, name, messageValue);
  return error;
}

static xt_value xt_error_ctor_invoke(xt_value thisValue, xt_value env, int32_t argc,
                                     xt_value *argv) {
  (void)thisValue;
  xt_value name = xt_closure_env(env, 0);
  return xt_make_error(xt_string_data(name), argc, argv);
}

/* The first-class value of a family constructor (used by `instanceof`,
 * `typeof`, and `class X extends Error`). Cached on the prototype. */
xt_value xt_error_constructor(xt_value nameValue) {
  const char *name = XT_IS_STRING(nameValue) ? xt_string_data(nameValue) : "Error";
  if (!xt_error_is_family(name)) name = "Error";
  xt_value proto = xt_error_proto_for(name);
  xt_value key = xt_string_from_cstr("__ctor");
  /* Own-property lookup only: a prototype walk would find `Error`'s
   * constructor (stored on the root prototype) for every child. */
  xt_property *existing =
      xt_object_find_property((xt_object *)XT_GET_PTR(proto), key);
  if (existing && XT_IS_FUNCTION(existing->value)) return existing->value;
  xt_value env[1];
  env[0] = xt_string_from_cstr(name);
  xt_value closure = xt_closure_new((void *)xt_error_ctor_invoke, 1, env);
  xt_function_set_prototype(closure, proto);
  xt_function_set_metadata(closure, env[0], 1);
  xt_object_set(proto, key, closure);
  return closure;
}

xt_value xt_error_to_string(xt_value error) {
  xt_value name = xt_object_get(error, xt_string_from_cstr("name"));
  xt_value message = xt_object_get(error, xt_string_from_cstr("message"));
  xt_value nameStr = name == XT_UNDEFINED ? xt_string_from_cstr("Error") : xt_to_string(name);
  xt_value messageStr =
      message == XT_UNDEFINED ? xt_string_from_cstr("") : xt_to_string(message);
  xt_string *ns = xt_as_string(nameStr);
  xt_string *ms = xt_as_string(messageStr);
  if (ns->length == 0) return messageStr;
  if (ms->length == 0) return nameStr;
  size_t total = ns->length + 2 + ms->length;
  char *buffer = (char *)malloc(total + 1);
  memcpy(buffer, xt_string_bytes(ns), ns->length);
  buffer[ns->length] = ':';
  buffer[ns->length + 1] = ' ';
  memcpy(buffer + ns->length + 2, xt_string_bytes(ms), ms->length);
  buffer[total] = '\0';
  xt_value result = xt_string_new(buffer, total);
  free(buffer);
  return result;
}

xt_value xt_error_ctor(int32_t argc, xt_value *argv) {
  return xt_make_error("Error", argc, argv);
}

xt_value xt_type_error_ctor(int32_t argc, xt_value *argv) {
  return xt_make_error("TypeError", argc, argv);
}

xt_value xt_range_error_ctor(int32_t argc, xt_value *argv) {
  return xt_make_error("RangeError", argc, argv);
}

xt_value xt_syntax_error_ctor(int32_t argc, xt_value *argv) {
  return xt_make_error("SyntaxError", argc, argv);
}

xt_value xt_reference_error_ctor(int32_t argc, xt_value *argv) {
  return xt_make_error("ReferenceError", argc, argv);
}

xt_value xt_eval_error_ctor(int32_t argc, xt_value *argv) {
  return xt_make_error("EvalError", argc, argv);
}

xt_value xt_uri_error_ctor(int32_t argc, xt_value *argv) {
  return xt_make_error("URIError", argc, argv);
}

xt_value xt_aggregate_error_ctor(int32_t argc, xt_value *argv) {
  return xt_make_error("AggregateError", argc, argv);
}

/* `super(message)` inside a subclass of the builtin Error family: initialize
 * the already-allocated instance instead of creating a fresh object. The
 * parent's name selects the default `name` (`TypeError`, ...). */
xt_value xt_error_init(xt_value thisValue, xt_value nameValue, int32_t argc,
                       xt_value *argv) {
  const char *name = XT_IS_STRING(nameValue) ? xt_string_data(nameValue) : "Error";
  if (!xt_error_is_family(name)) name = "Error";
  /* `class MyError extends Error` allocates a plain object first; mark it as an
   * Error so `.stack` and the error-specific property access apply. */
  if (XT_IS_OBJECT(thisValue)) {
    ((xt_object *)XT_GET_PTR(thisValue))->header.kind = XT_OBJECT_KIND_ERROR;
  }
  xt_object_set(thisValue, xt_string_from_cstr("name"), xt_string_from_cstr(name));
  xt_value message;
  if (strcmp(name, "AggregateError") == 0) {
    xt_object_set(thisValue, xt_string_from_cstr("errors"), xt_arg_at(argc, argv, 0));
    message = xt_arg_at(argc, argv, 1);
  } else {
    message = xt_arg_at(argc, argv, 0);
  }
  xt_value messageValue = message == XT_UNDEFINED ? xt_string_from_cstr("") : message;
  xt_object_set(thisValue, xt_string_from_cstr("message"), messageValue);
  xt_error_capture_stack(thisValue, name, messageValue);
  return thisValue;
}
