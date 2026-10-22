# 类型化数组与二进制数据

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/typed-arrays.md)

实现位置：`runtime/xt_typed_array.c`、`runtime/xt_array_buffer.c`。

## 类型化数组

- `Uint8Array`、`Int8Array`、`Uint8ClampedArray`、`Uint16Array`、`Int16Array`、`Uint32Array`、`Int32Array`、`Float32Array`、`Float64Array`。
- 构造：`new X(n)` / `new X(可迭代)` / `new X(buffer[, offset[, length]])` / `X.from` / `X.of`。
- 属性：`length` / `byteLength` / `byteOffset` / `.buffer` / `BYTES_PER_ELEMENT`。
- 元素读写按类型强制（`ToIntN` / `ToUintN`、`Uint8ClampedArray` 舍入、`Float32` 单精度舍入）。
- 原型方法：`fill` / `set` / `slice` / `subarray` / `join` / `toString` / `indexOf` / `lastIndexOf` / `includes` / `forEach` / `map` / `filter` / `every` / `some` / `find` / `findIndex` / `reduce` / `reverse` / `sort` / `copyWithin` / `at` / `keys` / `values` / `entries`。迭代器方法仍返回数组，而非迭代器对象。
- 类型化数组是 `ArrayBuffer` 上的视图；`subarray` 共享缓冲区，`slice` 复制。

## ArrayBuffer 与 DataView

- `ArrayBuffer`：`byteLength`、`slice`、`ArrayBuffer.isView`。
- `DataView`：默认大端，可选 `littleEndian`，支持 Uint8 / Int8 / Uint16 / Int16 / Uint32 / Int32 / Float32 / Float64 的 get/set。
- 同一缓冲区上的多个视图共享字节。

## 未实现

- `BigInt64Array` / `BigUint64Array`。
- 基于 `Buffer` 的类型化数组视图。
- `TextEncoder` / `TextDecoder` 已实现（UTF-8），文档见[字符串与文本](./strings.md)；`structuredClone` 会复制类型化数组。
