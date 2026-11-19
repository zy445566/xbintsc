/*
 * xbintsc runtime — interned C-string property lookups.
 *
 * Included by xt_containers.c after objects.inc; see xt_object_get_cstr`n * there for why the table exists.
 */

/* Convenience used by codegen for `obj.prop`.
 *
 * The C-string property lookups a host makes (`xt_object_get_cstr(w, "length")`
 * from the GUI engine, the typed-array helpers, every runtime helper) used to
 * allocate a fresh `xt_string` on every call. A three.js page made millions of
 * them per second, which was most of the collector's allocation pressure. The
 * table below interns the string by contents, so a repeated lookup reuses one
 * heap string; the strings are roots because the table itself is invisible to
 * the collector. */
#define XT_CSTR_CACHE_SLOTS 1024

static xt_value g_cstr_cache[XT_CSTR_CACHE_SLOTS];
static int g_cstr_cache_ready = 0;

xt_value xt_object_get_cstr(xt_value value, const char *key) {
  if (key == NULL) return xt_object_get(value, XT_UNDEFINED);
  if (xt_profile_on) xt_profile_note_cstr(key);
  if (!g_cstr_cache_ready) {
    /* Registered before the first entry is stored, so a collection can never
     * see a half-populated cache. */
    xt_gc_add_root_range(g_cstr_cache, XT_CSTR_CACHE_SLOTS);
    g_cstr_cache_ready = 1;
  }
  const size_t length = strlen(key);
  uint64_t hash = 1469598103934665603ull;
  for (size_t i = 0; i < length; i++) {
    hash ^= (unsigned char)key[i];
    hash *= 1099511628211ull;
  }
  const size_t start = (size_t)(hash & (XT_CSTR_CACHE_SLOTS - 1));
  for (size_t probe = 0; probe < 4; probe++) {
    xt_value slot = g_cstr_cache[(start + probe) & (XT_CSTR_CACHE_SLOTS - 1)];
    if (slot == 0) {
      xt_value created = xt_string_from_cstr(key);
      g_cstr_cache[(start + probe) & (XT_CSTR_CACHE_SLOTS - 1)] = created;
      return xt_object_get(value, created);
    }
    if (xt_string_length_value(slot) == (int32_t)length && XT_IS_STRING(slot)) {
      const char *text = xt_string_data(slot);
      if (text != NULL && memcmp(text, key, length) == 0) return xt_object_get(value, slot);
    }
  }
  /* Four probes collided: replace the primary slot. The evicted string stays
   * alive while anything references it; a later lookup simply interns it
   * again. */
  xt_value created = xt_string_from_cstr(key);
  g_cstr_cache[start] = created;
  return xt_object_get(value, created);
}

