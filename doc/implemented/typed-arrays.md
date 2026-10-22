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

## ArrayBuffer and DataView

- `ArrayBuffer`: `byteLength`, `slice`, `ArrayBuffer.isView`.
- `DataView`: big-endian by default, optional `littleEndian`, with `get` / `set` for Uint8 / Int8 / Uint16 / Int16 / Uint32 / Int32 / Float32 / Float64.
- Multiple views over one buffer share its bytes.

## Not implemented

- `BigInt64Array` / `BigUint64Array`.
- `Buffer`-backed typed-array views.
- `TextEncoder` / `TextDecoder` are implemented (UTF-8) and documented in [Strings and text](./strings.md); `structuredClone` copies typed arrays.
