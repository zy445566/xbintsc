/*
 * Function values and their `.prototype` objects.
 *
 * Split out of `objects.inc` to keep both files inside the repository's
 * length budget; included before it because `xt_object_get` resolves
 * `fn.prototype` through `xt_function_prototype_object`.
 */

/* The `.prototype` object of a function value, created on first use with a
 * `constructor` back-reference. `xt_new` and `fn.prototype` must agree on it so
 * the ES5 pattern `function Foo() {}; Foo.prototype.bar = ...` keeps working.
 *
 * Every reference to a declared function builds a fresh closure value (`Foo`
 * inside `Foo.prototype` is not the same value as the `Foo` used by `new`), so
 * the prototype is keyed by the compiled code pointer and shared by all
 * closures of that declaration. Without this, `new Foo()` had a different
 * prototype than `Foo.prototype` and instance methods were invisible. */
typedef struct xt_function_proto_slot {
  xt_code_fn code;
  xt_value proto;
  struct xt_function_proto_slot *next;
} xt_function_proto_slot;

static xt_function_proto_slot *xt_function_proto_slots = NULL;
static int xt_function_proto_registered = 0;

static void xt_function_proto_roots(void) {
  for (xt_function_proto_slot *slot = xt_function_proto_slots; slot; slot = slot->next) {
    xt_gc_mark_value(slot->proto);
  }
}

xt_value xt_function_prototype_object(xt_value value) {
  if (!XT_IS_FUNCTION(value)) return XT_UNDEFINED;
  xt_function *function = (xt_function *)XT_GET_PTR(value);
  if (function->prototype != XT_UNDEFINED) return function->prototype;
  if (!xt_function_proto_registered) {
    xt_function_proto_registered = 1;
    xt_gc_register_root_provider(xt_function_proto_roots);
  }
  for (xt_function_proto_slot *slot = xt_function_proto_slots; slot; slot = slot->next) {
    if (slot->code == function->code) {
      function->prototype = slot->proto;
      return slot->proto;
    }
  }
  xt_value proto = xt_object_new();
  xt_object_set(proto, xt_string_from_cstr("constructor"), value);
  xt_function_proto_slot *slot = (xt_function_proto_slot *)malloc(sizeof(xt_function_proto_slot));
  if (slot) {
    slot->code = function->code;
    slot->proto = proto;
    slot->next = xt_function_proto_slots;
    xt_function_proto_slots = slot;
  }
  function->prototype = proto;
  return proto;
}
