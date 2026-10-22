# 集合

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/collections.md)

实现位置：`runtime/xt_stdlib2/container.inc`（Map / Set）与
`runtime/xt_weak.c`（WeakMap / WeakSet / WeakRef）。

## Map 与 Set

- `new Map()` / `new Map(iterable)`、`new Set()` / `new Set(iterable)`。
- `Map`：`size`、`get` / `set` / `has` / `delete` / `clear` / `forEach` / `keys` / `values` / `entries`。
- `Set`：`size`、`add` / `has` / `delete` / `clear` / `forEach` / `values` / `keys` / `entries`。
- `for...of` 与展开通过 `[Symbol.iterator]` 迭代；`Map` 产出 `[key, value]` 对，`keys` / `values` / `entries` 按插入序返回数组。
- `Object.keys` / `values` / `entries` 不会看到集合内部结构。
- `structuredClone` 支持 `Map` 与 `Set`，保留循环引用与共享引用。

## WeakMap、WeakSet 与 WeakRef

- `WeakMap`：`get` / `set` / `has` / `delete`；`WeakSet`：`add` / `has` / `delete`；`WeakRef`：`deref()`。
- 键（以及 `WeakRef` 目标）不会让被引用对象存活：`WeakMap` / `WeakSet` 的键是 **ephemeron**（只有键可达时才保留值），`WeakRef` 目标不可达时会被清空。
- 栈扫描是保守的，残留的栈字可能延迟弱目标清空。完整 GC 描述见[运行时偏差](../unimplemented/deviations.md)与[未实现：工具链](../unimplemented/toolchain.md)。

## 迭代

- 任何暴露 `[Symbol.iterator]()` 的对象都可在 `for...of` 与展开中使用。`Array.prototype[Symbol.iterator]` 稳定，调用后返回带 `next()` 且自身可迭代的迭代器对象。
- 迭代细节另见[代码生成](./codegen.md)与 [Symbol](./stdlib.md)。
