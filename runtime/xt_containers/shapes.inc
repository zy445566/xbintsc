/* ------------------------------------------------------------------------- */
/* Hidden classes                                                            */
/* ------------------------------------------------------------------------- */

/* Shapes are interned by their (parent, property key) transition. They are
 * immortal metadata, but their keys are ordinary strings/symbols, so a root
 * provider keeps those keys alive. A key is matched by *text* (see
 * `xt_shape_bucket`), so an interned literal and a string built at run time with
 * the same bytes share one transition.
 *
 * The bucket count and the intern limit are performance parameters, not
 * semantics: a saturated table (or a NULL shape) disables the shape/method
 * inline caches for every object created afterwards, so the next lookup is a
 * linear scan of the receiver and its prototype chain. A GUI page built on a
 * large JS bundle interns tens of thousands of transitions, so the old 256
 * buckets / 16K shapes pair walked long chains and then silently degraded the
 * hot paths; both are sized for a large application now.
 */
#define XT_SHAPE_BUCKETS 4096
/* Interned shapes are immortal metadata. A program that creates property names
 * dynamically would otherwise leak one shape per name forever, so stop
 * interning after a bounded number; those objects simply fall back to the
 * uncached lookup (a NULL shape). */
#define XT_SHAPE_LIMIT 262144
static xt_shape *g_shapes[XT_SHAPE_BUCKETS];
static xt_shape *g_shape_all = NULL;
static xt_shape *g_shape_root = NULL;
static uint32_t g_shape_count = 0;

/* A key's *content* decides its bucket, not the address of the string that
 * holds it. Property keys built at run time (a typed array's `byteLength`, a
 * DOM handle's `length`, any `obj["k" + i]`) are fresh string objects with the
 * same bytes; hashing the pointer would put each of them in its own bucket, so
 * the value comparison in the chain below could never find the existing
 * transition and *every insert* would intern a new shape. Hashing the bytes is
 * what makes value interning work, and therefore what keeps the shape table
 * (and the inline caches that depend on it) usable in a large program. */
static uint64_t xt_shape_key_hash(xt_value key) {
  if (XT_IS_STRING(key)) {
    xt_string *string = xt_as_string(key);
    const char *bytes = xt_string_bytes(string);
    uint64_t hash = 1469598103934665603ULL; /* FNV-1a */
    for (uint32_t i = 0; i < string->length; i++) {
      hash ^= (unsigned char)bytes[i];
      hash *= 1099511628211ULL;
    }
    return hash;
  }
  return (uint64_t)(uintptr_t)XT_GET_PTR(key);
}

static size_t xt_shape_bucket(xt_shape *parent, xt_value key) {
  uintptr_t mixed = ((uintptr_t)parent >> 4) ^ (uintptr_t)xt_shape_key_hash(key);
  /* The low bits are alignment, not entropy: mix them before masking. */
  mixed ^= mixed >> 17;
  mixed *= 0x9e3779b97f4a7c15ULL;
  return (size_t)(mixed >> 32) & (XT_SHAPE_BUCKETS - 1);
}

static void xt_shape_gc_roots(void) {
  for (xt_shape *shape = g_shape_all; shape; shape = shape->all_next) xt_gc_mark_value(shape->key);
}

static xt_shape *xt_shape_empty(void) {
  if (g_shape_root) return g_shape_root;
  xt_shape *shape = (xt_shape *)calloc(1, sizeof(xt_shape));
  if (!shape) abort();
  shape->key = XT_UNDEFINED;
  g_shape_root = shape;
  g_shape_all = shape;
  xt_gc_register_root_provider(xt_shape_gc_roots);
  return shape;
}

static int xt_shape_trace_enabled(void) {
  static int enabled = -1;
  if (enabled < 0) {
    const char *env = getenv("XT_SHAPE_TRACE");
    enabled = (env != NULL && env[0] != '\0' && env[0] != '0') ? 1 : 0;
  }
  return enabled;
}

static uint64_t g_shape_rebuilds = 0;

/* Trace-only histogram over the property names that create transitions. */
typedef struct {
  char text[24];
  uint32_t count;
} xt_shape_key_stat;
#define XT_SHAPE_TRACE_KEYS 64
static xt_shape_key_stat g_shape_key_stats[XT_SHAPE_TRACE_KEYS];
static uint32_t g_shape_key_stat_count = 0;

static void xt_shape_trace_record(xt_value key) {
  char text[24];
  if (XT_IS_STRING(key)) {
    xt_string *string = xt_as_string(key);
    const char *bytes = xt_string_bytes(string);
    uint32_t length = string->length;
    if (length > 20) length = 20;
    if (bytes) {
      memcpy(text, bytes, length);
      text[length] = '\0';
    } else {
      text[0] = '\0';
    }
  } else if (xt_is_symbol(key)) {
    xt_value description = xt_symbol_description(key);
    const char *name = XT_IS_STRING(description) ? xt_string_bytes(xt_as_string(description)) : "";
    snprintf(text, sizeof(text), "@%s", name != NULL ? name : "");
  } else {
    snprintf(text, sizeof(text), "<other>");
  }
  for (uint32_t i = 0; i < g_shape_key_stat_count; i++) {
    if (strcmp(g_shape_key_stats[i].text, text) == 0) {
      g_shape_key_stats[i].count++;
      return;
    }
  }
  if (g_shape_key_stat_count < XT_SHAPE_TRACE_KEYS) {
    snprintf(g_shape_key_stats[g_shape_key_stat_count].text, sizeof(g_shape_key_stats[0].text), "%s", text);
    g_shape_key_stats[g_shape_key_stat_count].count = 1;
    g_shape_key_stat_count++;
  }
}

static void xt_shape_trace_report(void) {
  fprintf(stderr, "xt_shape: %u transitions, %llu rebuilds; top property names:", (unsigned)g_shape_count,
          (unsigned long long)g_shape_rebuilds);
  for (int rank = 0; rank < 8; rank++) {
    int best = -1;
    for (uint32_t i = 0; i < g_shape_key_stat_count; i++) {
      if (g_shape_key_stats[i].count == 0) continue;
      if (best < 0 || g_shape_key_stats[i].count > g_shape_key_stats[best].count) best = (int)i;
    }
    if (best < 0) break;
    fprintf(stderr, " %s=%u", g_shape_key_stats[best].text, g_shape_key_stats[best].count);
    g_shape_key_stats[best].count = 0;
  }
  fprintf(stderr, "\n");
}

/* Print the full property layout of a newly interned shape (root first). */
static void xt_shape_trace_chain(xt_shape *shape) {
  const xt_shape *chain[64];
  int depth = 0;
  for (const xt_shape *s = shape; s != NULL && s->parent != NULL && depth < 64; s = s->parent) chain[depth++] = s;
  fprintf(stderr, "xt_shape: latest layout:");
  for (int i = depth - 1; i >= 0; i--) {
    xt_value key = chain[i]->key;
    if (XT_IS_STRING(key)) {
      xt_string *string = xt_as_string(key);
      const char *bytes = xt_string_bytes(string);
      uint32_t length = string->length;
      if (length > 16) length = 16;
      fprintf(stderr, " %.*s", (int)length, bytes != NULL ? bytes : "");
    } else if (xt_is_symbol(key)) {
      xt_value description = xt_symbol_description(key);
      const char *name = XT_IS_STRING(description) ? xt_string_bytes(xt_as_string(description)) : "?";
      fprintf(stderr, " @%s", name != NULL ? name : "?");
    } else {
      fprintf(stderr, " <other>");
    }
  }
  fprintf(stderr, "\n");
}

xt_shape *xt_shape_transition(xt_shape *parent, xt_value key) {
  if (!parent) parent = xt_shape_empty();
  size_t bucket = xt_shape_bucket(parent, key);
  for (xt_shape *shape = g_shapes[bucket]; shape; shape = shape->next) {
    if (shape->parent != parent) continue;
    /* Property keys are interned for literals, but a key built at run time
     * (`obj[name]`, a number converted to a string, a symbol-less parser) is a
     * fresh value every time. Interning by value, not by pointer, is what keeps
     * a page from creating one shape per *insert* instead of one per distinct
     * layout. */
    if (shape->key == key || xt_property_key_equals(shape->key, key)) return shape;
  }
  if (g_shape_count >= XT_SHAPE_LIMIT) return NULL;
  xt_shape *shape = (xt_shape *)calloc(1, sizeof(xt_shape));
  if (!shape) abort();
  shape->key = key;
  shape->count = parent->count + 1;
  shape->parent = parent;
  shape->next = g_shapes[bucket];
  g_shapes[bucket] = shape;
  shape->all_next = g_shape_all;
  g_shape_all = shape;
  g_shape_count++;
  /* Diagnostics: `XT_SHAPE_TRACE=1` reports the interned-transition count so a
   * page that needs a larger table is visible instead of silently degrading.
   * The per-key histogram shows *which* property names are multiplying. */
  if (xt_shape_trace_enabled()) {
    xt_shape_trace_record(key);
    if ((g_shape_count & 32767u) == 0) {
      xt_shape_trace_report();
      xt_shape_trace_chain(shape);
    }
  }
  return shape;
}

void xt_object_shape_changed(xt_object *obj) {
  g_shape_rebuilds++;
  xt_shape *shape = xt_shape_empty();
  for (uint32_t i = 0; i < obj->count; i++) {
    shape = xt_shape_transition(shape, obj->properties[i].key);
    if (!shape) {
      obj->shape = NULL;
      return;
    }
  }
  obj->shape = shape;
}
