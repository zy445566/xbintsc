/*
 * xbintsc runtime — a subset of `Proxy`.
 *
 * A proxy is an ordinary host view (`xt_object` with hooks): its target and
 * handler live in hidden symbol-keyed properties, so the collector traces them
 * like any other property, and the existing hook dispatch in `xt_object_get`,
 * `xt_object_set`, `xt_object_has`, `xt_delete` and `xt_object_keys` runs the
 * traps. Own and prototype lookups on the proxy object itself stay empty, so
 * every string-keyed read and write misses and reaches the hooks.
 *
 * Supported traps: `get`, `set`, `has`, `deleteProperty`, `ownKeys`. Without a
 * trap the operation is forwarded to the target. Not supported (and documented
 * in doc/unimplemented/expressions.md): function targets (`apply`/`construct`),
 * `Object.getPrototypeOf`/`setPrototypeOf` traps, property-descriptor traps
 * (`getOwnPropertyDescriptor`, `defineProperty`, `isExtensible`, …) and
 * `Proxy.revocable`.
 */

#include "rt_internal.h"

#include <string.h>

/* Hidden own properties; symbol keys keep them out of `Object.keys`, JSON and
 * ordinary string-keyed reads. */
static xt_value xt_proxy_target_key = XT_UNDEFINED;
static xt_value xt_proxy_handler_key = XT_UNDEFINED;

static xt_value xt_proxy_state_key(const char *name, xt_value *slot) {
  if (!xt_is_symbol(*slot)) {
    *slot = xt_symbol_new(xt_string_from_cstr(name));
    xt_gc_add_root(slot);
  }
  return *slot;
}

static xt_value xt_proxy_target(xt_value proxy) {
  return xt_object_get(proxy, xt_proxy_state_key("__xt_proxy_target", &xt_proxy_target_key));
}

static xt_value xt_proxy_handler(xt_value proxy) {
  return xt_object_get(proxy, xt_proxy_state_key("__xt_proxy_handler", &xt_proxy_handler_key));
}

/* The handler's function for `name`, or 0 when the trap is absent. */
static int xt_proxy_trap(xt_value proxy, const char *name, xt_value *trap) {
  xt_value handler = xt_proxy_handler(proxy);
  if (!XT_IS_OBJECT(handler)) return 0;
  xt_value value = xt_object_get(handler, xt_string_from_cstr(name));
  if (!XT_IS_FUNCTION(value)) return 0;
  *trap = value;
  return 1;
}

/* -- hooks ---------------------------------------------------------------- */

static xt_value xt_proxy_hook_get(xt_value self, xt_value key, void *data) {
  (void)data;
  xt_value target = xt_proxy_target(self);
  xt_value trap;
  if (!xt_proxy_trap(self, "get", &trap)) return xt_object_get(target, key);
  xt_value arguments[3] = {target, key, self};
  return xt_call_with_this(trap, xt_proxy_handler(self), 3, arguments);
}

static int xt_proxy_hook_set(xt_value self, xt_value key, xt_value value, void *data) {
  (void)data;
  xt_value target = xt_proxy_target(self);
  xt_value trap;
  if (!xt_proxy_trap(self, "set", &trap)) {
    xt_object_set(target, key, value);
    return 1;
  }
  xt_value arguments[4] = {target, key, value, self};
  xt_call_with_this(trap, xt_proxy_handler(self), 4, arguments);
  /* A falsy trap result would fail the assignment in strict mode; the runtime
   * has no strict/sloppy distinction, so a handled write always succeeds. */
  return 1;
}

static int xt_proxy_hook_has(xt_value self, xt_value key, void *data) {
  (void)data;
  xt_value target = xt_proxy_target(self);
  xt_value trap;
  if (!xt_proxy_trap(self, "has", &trap)) return xt_truthy(xt_object_has(target, key)) ? 1 : 0;
  xt_value arguments[2] = {target, key};
  return xt_truthy(xt_call_with_this(trap, xt_proxy_handler(self), 2, arguments)) ? 1 : 0;
}

static int xt_proxy_hook_remove(xt_value self, xt_value key, void *data) {
  (void)data;
  xt_value target = xt_proxy_target(self);
  xt_value trap;
  if (!xt_proxy_trap(self, "deleteProperty", &trap)) {
    xt_delete(target, key);
    return 1;
  }
  xt_value arguments[2] = {target, key};
  xt_call_with_this(trap, xt_proxy_handler(self), 2, arguments);
  return 1;
}

static xt_value xt_proxy_hook_keys(xt_value self, void *data) {
  (void)data;
  xt_value target = xt_proxy_target(self);
  xt_value trap;
  if (!xt_proxy_trap(self, "ownKeys", &trap)) return xt_object_keys(target);
  xt_value arguments[1] = {target};
  return xt_call_with_this(trap, xt_proxy_handler(self), 1, arguments);
}

static const xt_object_hooks xt_proxy_hooks = {
  xt_proxy_hook_get,
  xt_proxy_hook_set,
  xt_proxy_hook_has,
  xt_proxy_hook_remove,
  xt_proxy_hook_keys,
};

/* -- construction --------------------------------------------------------- */

xt_value xt_proxy_ctor(int32_t argc, xt_value *argv) {
  xt_value target = xt_arg(argc, argv, 0);
  xt_value handler = xt_arg(argc, argv, 1);
  if (!XT_IS_OBJECT(target) && !XT_IS_FUNCTION(target)) {
    xt_throw(xt_error_from_message("TypeError: Cannot create proxy with a non-object as target"));
    return XT_UNDEFINED;
  }
  if (!XT_IS_OBJECT(handler)) {
    xt_throw(xt_error_from_message("TypeError: Cannot create proxy with a non-object as handler"));
    return XT_UNDEFINED;
  }
  xt_value proxy = xt_object_new();
  /* The proxy inherits the target's prototype: `instanceof` and inherited
   * methods behave like the target's. A `getPrototypeOf` trap is not modelled. */
  xt_object_set_prototype(proxy, xt_object_get_prototype(target));
  xt_object_set(proxy, xt_proxy_state_key("__xt_proxy_target", &xt_proxy_target_key), target);
  xt_object_set(proxy, xt_proxy_state_key("__xt_proxy_handler", &xt_proxy_handler_key), handler);
  xt_object_set_hooks(proxy, &xt_proxy_hooks, NULL);
  return proxy;
}
