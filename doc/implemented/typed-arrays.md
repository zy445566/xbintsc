# Typed arrays and binary data

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/typed-arrays.md)

Location: `runtime/xt_typed_array.c`, `runtime/xt_array_buffer.c`.

## Typed arrays

- `Uint8Array`, `Int8Array`, `Uint8ClampedArray`, `Uint16Array`, `Int16Array`, `Uint32Array`, `Int32Array`, `Float32Array`, `Float64Array`.
- Construction: `new X(n)` / `new X(iterable)` / `new X(buffer[, offset[, length]])` / `X.from` / `X.of`.
- Properties: `length` / `byteLength` / `byteOffset` / `.buffer` / `BYTES_PER_ELEMENT`.
- Element reads/writes apply the per-type coercion (`ToIntN` / `ToUintN`, `Uint8ClampedArray` rounding, `Float32` single-precision rounding).
- Prototype methods: `fill` / `set` / `slice` / `subarray` / `join` / `toString` / `indexOf` / `lastIndexOf` / `includes` / `forEach` / `map` / `filter` / `every` / `some` / `find` / `findIndex` / `reduce` / `reverse` / `sort` / `copyWithin` / `at` / `keys` / `values` / `entries`. Iterator methods still return arrays, not iterator objects.
- Typed arrays are views over an `ArrayBuffer`; `subarray` shares the buffer and `slice` copies.
- Element access is direct memory access, not a property lookup: an `ArrayBuffer`
  owns one `uint8_t` block (allocation kind `XT_OBJECT_KIND_ARRAY_BUFFER`), and a
  view resolves that block plus its byte offset once at construction (`view_bytes`
  in `xt_object`) and then indexes it with a `memcpy`-sized read/write. The
  general `xt_get`/`xt_set` path is only used for non-index keys and for views
  without a real buffer. This is what makes geometry code (three.js reading and
  writing `position`/`normal` attribute arrays) run at memory speed instead of
  paying a runtime call per byte; the same work used to be ~50x slower than a
  plain array.
- Only canonical array indices (`0 .. 2^32-2`, integral) name an element:
  `ta[1.5]`, `ta[-1]` and `ta[2**32]` are ordinary property accesses, and an
  out-of-range index reads `undefined` / ignores the write.

## ArrayBuffer and DataView

- `ArrayBuffer`: `byteLength`, `slice`, `ArrayBuffer.isView`. `byteLength` and
  `slice` are served by the runtime (`xt_get`), not stored as own properties, so
  `Object.keys(new ArrayBuffer(8))` is empty like on the platform.
- `DataView`: big-endian by default, optional `littleEndian`, with `get` / `set` for Uint8 / Int8 / Uint16 / Int16 / Uint32 / Int32 / Float32 / Float64.
- Multiple views over one buffer share its bytes. The collector frees a buffer's
  byte block when the buffer is swept; a view keeps its buffer reachable, so the
  cached base pointer in a view never dangles.

## Not implemented

- `BigInt64Array` / `BigUint64Array`.
- `Buffer`-backed typed-array views.
- `TextEncoder` / `TextDecoder` are implemented (UTF-8) and documented in [Strings and text](./strings.md); `structuredClone` copies typed arrays.
