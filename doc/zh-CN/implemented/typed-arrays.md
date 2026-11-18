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
- 元素访问是直接内存访问，而不是属性查找：`ArrayBuffer` 持有一整块 `uint8_t`
  字节（分配种类 `XT_OBJECT_KIND_ARRAY_BUFFER`），视图在构造时解析出该内存块
  与自身字节偏移（`xt_object` 中的 `view_bytes`），之后用与元素宽度等长的
  `memcpy` 读写。只有非索引键、以及没有真实缓冲区的视图才走通用的
  `xt_get`/`xt_set` 路径。几何类代码（three.js 读写 `position`/`normal`）
  因此能以内存速度运行，而不必为每个字节付一次运行时调用；同样的工作此前比
  普通数组慢约 50 倍。
- 只有规范数组索引（`0 .. 2^32-2` 的整数）才表示元素：`ta[1.5]`、`ta[-1]`、
  `ta[2**32]` 都是普通属性访问，越界索引读取为 `undefined` / 写入被忽略。

## ArrayBuffer 与 DataView

- `ArrayBuffer`：`byteLength`、`slice`、`ArrayBuffer.isView`。`byteLength` 与
  `slice` 由运行时（`xt_get`）提供，不是自有属性，因此
  `Object.keys(new ArrayBuffer(8))` 为空，与平台一致。
- `DataView`：默认大端，可选 `littleEndian`，支持 Uint8 / Int8 / Uint16 / Int16 / Uint32 / Int32 / Float32 / Float64 的 get/set。
- 同一缓冲区上的多个视图共享字节。缓冲区被回收时收集器释放其字节块；视图持有
  缓冲区引用，所以视图里缓存的基址指针不会悬空。

## 未实现

- `BigInt64Array` / `BigUint64Array`。
- 基于 `Buffer` 的类型化数组视图。
- `TextEncoder` / `TextDecoder` 已实现（UTF-8），文档见[字符串与文本](./strings.md)；`structuredClone` 会复制类型化数组。
