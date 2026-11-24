/* xbintsc runtime — the property-read inline cache.
 *
 * Included by `xt_containers.c` after `access.inc`; split out so the element
 * access unit stays within the repository's file-length budget. `rt.h` declares
 * `xt_get_ic`, which is what generated code calls.
 */

/* Inline-cached property read. The generated code gives every source-level
 * property access four zeroed `(shape, index)` pairs; a shape hit is a pointer
 * compare plus one key check, which avoids the property lookup entirely. Only
 * ordinary objects and errors use the cache; arrays, functions, strings and
 * containers fall back to the generic path. */
xt_value xt_get_ic(xt_value target, xt_value key, int64_t *cache) {
  XT_PROFILE_TICK(XT_PROFILE_GET_IC);
  if (!cache || !XT_IS_OBJECT(target)) {
    XT_PROFILE_TICK(XT_PROFILE_IC_EARLY);
    return xt_get(target, key);
  }
  xt_object *obj = (xt_object *)XT_GET_PTR(target);
  /* A host view answers keys dynamically, outside the shape cache; arrays,
   * functions and non-string/symbol keys take the generic dispatch too. */
  if (!xt_kind_is_object(obj->header.kind) || obj->hooks != NULL ||
      (!XT_IS_STRING(key) && !xt_is_symbol(key))) {
    XT_PROFILE_TICK(XT_PROFILE_IC_EARLY);
    return xt_get(target, key);
  }
  xt_shape *shape = obj->shape;
  /* Four entries, newest last: real call sites are polymorphic (a helper used
   * by several three.js classes, a generic layout routine), and a single entry
   * made almost every access at those sites miss and pay the full lookup. */
  if (shape != NULL) {
    for (int slot = 3; slot >= 0; slot--) {
      if ((xt_shape *)(uintptr_t)cache[slot * 2] != shape) continue;
      int64_t cachedIndex = cache[slot * 2 + 1];
      if (cachedIndex < 0 || cachedIndex >= (int64_t)obj->count) continue;
      xt_property *prop = &obj->properties[cachedIndex];
      if (prop->key == key || xt_property_key_equals(prop->key, key)) {
        XT_PROFILE_TICK(XT_PROFILE_IC_HIT);
        if (prop->getter != XT_UNDEFINED && XT_IS_FUNCTION(prop->getter)) {
          return xt_call_with_this(prop->getter, target, 0, NULL);
        }
        if (prop->getter != XT_UNDEFINED) return XT_UNDEFINED;
        return prop->value;
      }
    }
  }
  XT_PROFILE_TICK(XT_PROFILE_IC_MISS);
  xt_value propertyKey = xt_to_property_key(key);
  xt_property *prop = xt_object_find_property(obj, propertyKey);
  if (prop) {
    if (shape != NULL) {
      /* FIFO: the oldest entry falls out, so a site that cycles through more
       * than four layouts still converges instead of thrashing one slot. */
      cache[0] = cache[2];
      cache[1] = cache[3];
      cache[2] = cache[4];
      cache[3] = cache[5];
      cache[4] = cache[6];
      cache[5] = cache[7];
      cache[6] = (int64_t)(uintptr_t)shape;
      cache[7] = (int64_t)(prop - obj->properties);
    }
    if (prop->getter != XT_UNDEFINED && XT_IS_FUNCTION(prop->getter)) {
      return xt_call_with_this(prop->getter, target, 0, NULL);
    }
    if (prop->getter != XT_UNDEFINED) return XT_UNDEFINED;
    return prop->value;
  }
  return xt_get(target, key);
}
