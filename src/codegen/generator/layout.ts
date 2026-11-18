/**
 * Byte offsets into the runtime's container structs that generated code reads
 * directly.
 *
 * The element fast path (`element-inline.ts`) skips the generic dispatch by
 * looking at a value's tag and then loading the fields it needs — an array's
 * `items`/`length`, a typed-array view's `view_bytes`/`view_length`/kind — out of
 * the object header. That only works if the compiler knows the layout, so these
 * constants mirror `runtime/rt_object.h`; `tests/codegen/element-layout.test.ts`
 * compiles a C probe against the runtime header and fails when the two drift
 * apart.
 */

import { XT_ARRAY, XT_NUMBER_MASK, XT_OBJECT, XT_TAG_MASK } from "../values.js";

export const XT_LAYOUT = {
  /** `xt_header` size: the payload starts here in every heap object. */
  headerSize: 16,
  /** `xt_array.length` (uint32). */
  arrayLength: 16,
  /** `xt_array.items` (i64 pointer to the element vector). */
  arrayItems: 24,
  /** `xt_object.reserved2` (uint16): a typed-array view's `kind + 1`. */
  objectTypedKind: 50,
  /** `xt_object.view_bytes` (i64 raw pointer): the view's first element. */
  objectViewBytes: 96,
  /** `xt_object.view_length` (uint32): bytes available from `view_bytes`. */
  objectViewLength: 104,
} as const;

/** Tags and masks the inline guards compare against (`runtime/rt.h`). */
export const XT_INLINE_TAGS = {
  tagMask: XT_TAG_MASK,
  numberMask: XT_NUMBER_MASK,
  array: XT_ARRAY,
  object: XT_OBJECT,
} as const;

/** Typed-array element kinds, in `XT_TA_*` order (`runtime/xt_typed_array.c`). */
export const XT_TYPED_KINDS = [
  { name: "Uint8Array", width: 1 },
  { name: "Int8Array", width: 1 },
  { name: "Uint8ClampedArray", width: 1 },
  { name: "Uint16Array", width: 2 },
  { name: "Int16Array", width: 2 },
  { name: "Uint32Array", width: 4 },
  { name: "Int32Array", width: 4 },
  { name: "Float32Array", width: 4 },
  { name: "Float64Array", width: 8 },
] as const;

/** Index of `Float32Array` / `Float64Array` in {@link XT_TYPED_KINDS}. */
export const XT_KIND_F32 = 7;
export const XT_KIND_F64 = 8;
