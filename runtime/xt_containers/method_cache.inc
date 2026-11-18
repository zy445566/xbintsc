/*
 * Method calls with a monomorphic inline cache; included by xt_containers.c
 * after access.inc.
 */
/* ------------------------------------------------------------------------- */
/* Method-call inline cache                                                  */
/* ------------------------------------------------------------------------- */

/*
 * A method call used to resolve the callee on every invocation: the container
 * and built-in dispatch tables, the receiver's own property bag, then a walk up
 * the prototype chain (`new Vector3().applyMatrix4(m)` pays all of it per
 * vertex). A monomorphic site — the common case in numeric code — always lands
 * on the same *slot* of the same holder, so the cache records that holder and
 * slot and re-reads the slot on a hit:
 *
 *   - `cache[0]` is the holder: `0` means empty, `XT_UNDEFINED` the receiver
 *     itself, anything else is the prototype value the method was found on;
 *   - `cache[1]` is the holder's shape pointer and `cache[2]` the property
 *     index. The slot's key is compared on every hit, so a slot whose contents
 *     were deleted and reused cannot mis-dispatch;
 *   - the *value* is re-read rather than cached, so reassigning the method
 *     (`obj.f = other`) is observed immediately — exactly like the property IC.
 *
 * A hit still calls through `xt_call_with_this` (the closure ABI needs `this`),
 * but skips the resolution. The cache is filled *after* a slow-path call, so it
 * never changes what that call does; a receiver whose shape, holder or prototype
 * changed simply misses again.
 */
static void xt_method_cache_fill(int64_t *cache, xt_value target, xt_value name) {
  if (cache == NULL || !XT_IS_OBJECT(target)) return;
  xt_object *object = (xt_object *)XT_GET_PTR(target);
  if (object->header.kind != XT_OBJECT_KIND_OBJECT || object->hooks != NULL) return;
  /* Only the receiver and its immediate prototype can be cached: the hit path
   * checks `object->prototype == holder`, so a method found deeper in the chain
   * would always miss anyway. */
  xt_object *holder = object;
  xt_value holderValue = XT_UNDEFINED;
  xt_property *prop = xt_object_find(object, name);
  if (prop == NULL) {
    xt_value proto = object->prototype;
    if (!XT_IS_OBJECT(proto)) return;
    holderValue = proto;
    holder = (xt_object *)XT_GET_PTR(proto);
    if (holder->header.kind != XT_OBJECT_KIND_OBJECT || holder->hooks != NULL) return;
    prop = xt_object_find(holder, name);
  }
  if (prop == NULL) return;
  if (!XT_IS_FUNCTION(prop->value) || prop->getter != XT_UNDEFINED) return;
  cache[0] = (int64_t)holderValue;
  cache[1] = (int64_t)(uintptr_t)holder->shape;
  cache[2] = (int64_t)(prop - holder->properties);
}

xt_value xt_call_method_ic(xt_value target, xt_value name, int32_t argc, xt_value *argv,
                           int64_t *cache) {
  if (cache != NULL && (cache[0] != 0 || cache[1] != 0) && XT_IS_OBJECT(target)) {
    xt_object *object = (xt_object *)XT_GET_PTR(target);
    if (object->header.kind == XT_OBJECT_KIND_OBJECT && object->hooks == NULL) {
      xt_object *holder = NULL;
      int64_t holderValue = cache[0];
      if ((xt_value)holderValue == XT_UNDEFINED) {
        holder = object;
      } else if (XT_IS_OBJECT((xt_value)holderValue) && object->prototype == (xt_value)holderValue) {
        holder = (xt_object *)XT_GET_PTR((xt_value)holderValue);
        if (holder->header.kind != XT_OBJECT_KIND_OBJECT || holder->hooks != NULL) holder = NULL;
      }
      if (holder != NULL) {
        int64_t slot = cache[2];
        if (holder->shape == (xt_shape *)(uintptr_t)cache[1] && slot >= 0 && slot < (int64_t)holder->count) {
          xt_property *prop = &holder->properties[slot];
          if (prop->getter == XT_UNDEFINED && XT_IS_FUNCTION(prop->value) &&
              (prop->key == name || xt_property_key_equals(prop->key, name))) {
            return xt_call_with_this(prop->value, target, argc, argv);
          }
        }
      }
    }
  }
  xt_value result = xt_call_method(target, name, argc, argv);
  xt_method_cache_fill(cache, target, name);
  return result;
}
