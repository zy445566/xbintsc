/* ------------------------------------------------------------------------- */
/* Hidden classes                                                            */
/* ------------------------------------------------------------------------- */

/* Shapes are interned by their (parent, property key) transition. They are
 * immortal metadata, but their keys are ordinary strings/symbols, so a root
 * provider keeps those keys alive. Pointer identity is enough to share a
 * transition between the same interned literal; dynamically built keys simply
 * get their own shape, which is still a correct layout identity for the IC. */
#define XT_SHAPE_BUCKETS 256
/* Interned shapes are immortal metadata. A program that creates property names
 * dynamically would otherwise leak one shape per name forever, so stop
 * interning after a bounded number; those objects simply fall back to the
 * uncached lookup (a NULL shape). */
#define XT_SHAPE_LIMIT 16384
static xt_shape *g_shapes[XT_SHAPE_BUCKETS];
static xt_shape *g_shape_all = NULL;
static xt_shape *g_shape_root = NULL;
static uint32_t g_shape_count = 0;

static size_t xt_shape_bucket(xt_shape *parent, xt_value key) {
  uintptr_t mixed = ((uintptr_t)parent >> 4) ^ ((uintptr_t)XT_GET_PTR(key) >> 4);
  return (size_t)mixed & (XT_SHAPE_BUCKETS - 1);
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

xt_shape *xt_shape_transition(xt_shape *parent, xt_value key) {
  if (!parent) parent = xt_shape_empty();
  size_t bucket = xt_shape_bucket(parent, key);
  for (xt_shape *shape = g_shapes[bucket]; shape; shape = shape->next) {
    if (shape->parent == parent && shape->key == key) return shape;
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
  return shape;
}

void xt_object_shape_changed(xt_object *obj) {
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
