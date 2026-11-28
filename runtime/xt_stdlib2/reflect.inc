/*
 * `Reflect`: the runtime's object-manipulation helpers.
 *
 * The TUI keeps a stable proxy around its renderer and forwards every call
 * with `Reflect.get(...)`, so `Reflect` being a recognized-but-undefined Web
 * global (as it used to be) made `pi` fail while constructing the interactive
 * mode. The static dispatcher mirrors `xt_object_static`'s shape
 * `(name, argc, argv)`, and `xt_reflect_object` exposes the same methods as
 * first-class values (`const { get } = Reflect`).
 */

#include "rt_internal.h"

#include <string.h>

static xt_value xt_reflect_method_bridge(xt_value thisValue, xt_value env, int32_t argc,
                                         xt_value *argv);
static xt_value xt_reflect_object_value = 0;

static const char *const XT_REFLECT_METHODS[] = {
    "get", "set", "has", "deleteProperty", "getPrototypeOf", "setPrototypeOf",
    "defineProperty", "getOwnPropertyDescriptor", "ownKeys", "apply",
    "construct", "isExtensible", "preventExtensions",
};

/* The hidden property-descriptor flags symbol is runtime bookkeeping, not a
 * user-visible own symbol. */
static int xt_reflect_is_internal_symbol(xt_value value) {
  if (!xt_is_symbol(value)) return 0;
  xt_value description = xt_symbol_description(value);
  const char *text = xt_string_data(description);
  return text != NULL && strstr(text, "__propertyDescriptorFlags") != NULL;
}

static xt_value xt_reflect_own_keys(xt_value target) {
  xt_value keys = xt_object_own_property_names(target);
  xt_value symbols = xt_object_own_property_symbols(target);
  xt_array *list = xt_as_array(symbols);
  if (!list) return keys;
  for (uint32_t i = 0; i < list->length; i++) {
    if (xt_reflect_is_internal_symbol(list->items[i])) continue;
    xt_array_push(keys, list->items[i]);
  }
  return keys;
}

static xt_value xt_reflect_dispatch(xt_value nameValue, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(nameValue);
  if (!method) return XT_UNDEFINED;
  xt_value target = xt_arg_at(argc, argv, 0);
  xt_value key = xt_arg_at(argc, argv, 1);
  if (strcmp(method, "get") == 0) return xt_get(target, key);
  if (strcmp(method, "set") == 0) {
    xt_set(target, key, xt_arg_at(argc, argv, 2));
    return XT_TRUE;
  }
  if (strcmp(method, "has") == 0) return xt_in(key, target);
  if (strcmp(method, "deleteProperty") == 0) {
    xt_delete(target, key);
    return XT_TRUE;
  }
  if (strcmp(method, "getPrototypeOf") == 0) return xt_object_get_prototype(target);
  if (strcmp(method, "setPrototypeOf") == 0) {
    xt_object_set_prototype(target, key);
    return XT_TRUE;
  }
  if (strcmp(method, "defineProperty") == 0) {
    xt_object_static(xt_string_from_cstr("defineProperty"), argc, argv);
    return XT_TRUE;
  }
  if (strcmp(method, "getOwnPropertyDescriptor") == 0) {
    return xt_object_static(xt_string_from_cstr("getOwnPropertyDescriptor"), argc, argv);
  }
  if (strcmp(method, "ownKeys") == 0) return xt_reflect_own_keys(target);
  if (strcmp(method, "apply") == 0) {
    xt_value function = target;
    xt_value thisArg = xt_arg_at(argc, argv, 1);
    xt_array *arguments = xt_as_array(xt_arg_at(argc, argv, 2));
    if (!XT_IS_FUNCTION(function)) return XT_UNDEFINED;
    return xt_call_with_this(function, thisArg, arguments ? (int32_t)arguments->length : 0,
                             arguments ? arguments->items : NULL);
  }
  if (strcmp(method, "construct") == 0) {
    xt_array *arguments = xt_as_array(xt_arg_at(argc, argv, 1));
    return xt_new(target, arguments ? (int32_t)arguments->length : 0,
                  arguments ? arguments->items : NULL);
  }
  if (strcmp(method, "isExtensible") == 0) return XT_TRUE;
  if (strcmp(method, "preventExtensions") == 0) return XT_TRUE;
  return XT_UNDEFINED;
}

xt_value xt_reflect_call(xt_value nameValue, int32_t argc, xt_value *argv) {
  return xt_reflect_dispatch(nameValue, argc, argv);
}

static xt_value xt_reflect_method_bridge(xt_value thisValue, xt_value env, int32_t argc,
                                         xt_value *argv) {
  (void)thisValue;
  return xt_reflect_dispatch(xt_closure_env(env, 0), argc, argv);
}

xt_value xt_reflect_object(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (xt_reflect_object_value != 0) return xt_reflect_object_value;
  xt_value object = xt_object_new();
  for (size_t i = 0; i < sizeof(XT_REFLECT_METHODS) / sizeof(XT_REFLECT_METHODS[0]); i++) {
    xt_value name = xt_string_from_cstr(XT_REFLECT_METHODS[i]);
    xt_value closure = xt_closure_new((void *)xt_reflect_method_bridge, 1, &name);
    xt_function_set_metadata(closure, name, 1);
    xt_object_set(object, name, closure);
  }
  xt_reflect_object_value = object;
  xt_gc_add_root(&xt_reflect_object_value);
  return object;
}
