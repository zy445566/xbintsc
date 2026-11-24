/*
 * xbintsc runtime — object layout.
 *
 * Included by `rt_internal.h`, after the common `xt_header` and the value tags,
 * because every container kind below starts with that header. Keeping the
 * layouts in one small file makes it obvious what an "object" is in this runtime:
 * a header, a property bag with an interned hidden-class shape, a prototype, and
 * the per-kind side slots (an ArrayBuffer's bytes, a typed-array view's resolved
 * base, an Error's captured stack, a host view's hooks).
 */

#ifndef XT_RT_OBJECT_H
#define XT_RT_OBJECT_H

typedef struct {
  /* String or Symbol value (see `xt_to_property_key`). */
  xt_value key;
  xt_value value;
  /* Accessor properties (`get x()` / `set x(v)`) store the functions here;
   * when set, `value` is ignored and property access invokes the getter. */
  xt_value getter;
  xt_value setter;
} xt_property;

/* Hidden-class descriptor for ordinary objects. Shapes are interned by their
 * (parent, key) transition and never freed, so an object's shape pointer is a
 * stable layout identity. Shape keys are rooted by a collector provider in
 * xt_containers/shapes.inc. */
struct xt_shape_index;
typedef struct xt_shape {
  xt_value key;
  uint32_t count;
  struct xt_shape *parent;
  struct xt_shape *next;     /* hash-bucket chain */
  struct xt_shape *all_next; /* every shape, for the GC root provider */
  /* Lazily built key -> property-index table for this layout (see
   * `xt_shape_lookup`). NULL until the shape is first looked up in a
   * property-heavy object. The chain order and the object's property order are
   * the same by construction, so the index is a property position too. */
  struct xt_shape_index *index;
} xt_shape;

typedef struct {
  xt_header header;
  uint32_t count;
  uint32_t capacity;
  xt_property *properties;
  xt_shape *shape;
  xt_value prototype;
  uint8_t frozen;
  uint8_t is_array_base;
  uint16_t reserved2;
  /* Captured `Error.stack` for XT_OBJECT_KIND_ERROR, XT_UNDEFINED otherwise.
   * Deliberately not a property: `Object.keys`, `for...in` and JSON.stringify
   * must not see it, while `error.stack` returns it through `xt_get`. */
  xt_value error_stack;
  /* Optional dynamic-property view (host objects such as `element.style`); see
   * `xt_object_hooks` in `rt.h`. NULL for ordinary objects. */
  const xt_object_hooks *hooks;
  void *hook_data;
  /* XT_OBJECT_KIND_ARRAY_BUFFER only: the backing bytes, owned by this object
   * and freed by the collector when it is swept. Views resolve them through
   * `xt_array_buffer_bytes` and index them directly. */
  uint8_t *buffer_bytes;
  uint32_t buffer_length;
  /* Typed-array views only: the bytes this view indexes (the buffer's block at
   * the view's byte offset) and how many are available from there, set once in
   * `xt_ta_view` and read on the per-element path. A buffer's block never moves
   * while it is alive, and the view keeps its buffer alive as a property, so no
   * invalidation is needed. NULL for every other object. */
  uint8_t *view_bytes;
  uint32_t view_length;
} xt_object;

typedef struct {
  xt_header header;
  uint32_t length;
  uint32_t capacity;
  xt_value *items;
  /* Named (non-index) properties such as `raw` on template strings or
   * `index`/`input` on RegExp match results, stored in an `xt_object`. */
  xt_value extra;
  /* The array's [[Prototype]] (`Array.prototype` for ordinary arrays). Arrays
   * are a distinct allocation kind, so this field is what lets
   * `Object.getPrototypeOf([])` and `x instanceof <Array alias>` work. */
  xt_value prototype;
} xt_array;

#endif /* XT_RT_OBJECT_H */
