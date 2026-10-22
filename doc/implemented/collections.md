# Collections

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/collections.md)

Location: `runtime/xt_stdlib2/container.inc` (Map / Set) and
`runtime/xt_weak.c` (WeakMap / WeakSet / WeakRef).

## Map and Set

- `new Map()` / `new Map(iterable)` and `new Set()` / `new Set(iterable)`.
- `Map`: `size`, `get` / `set` / `has` / `delete` / `clear` / `forEach` / `keys` / `values` / `entries`.
- `Set`: `size`, `add` / `has` / `delete` / `clear` / `forEach` / `values` / `keys` / `entries`.
- `for...of` and spread iterate them through `[Symbol.iterator]`; a `Map` yields `[key, value]` pairs, and `keys` / `values` / `entries` return arrays in insertion order.
- `Object.keys` / `values` / `entries` do not see collection internals.
- `structuredClone` supports `Map` and `Set`, preserving cycles and shared references.

## WeakMap, WeakSet and WeakRef

- `WeakMap`: `get` / `set` / `has` / `delete`; `WeakSet`: `add` / `has` / `delete`; `WeakRef`: `deref()`.
- Keys (and `WeakRef` targets) do not keep their referents alive: `WeakMap` / `WeakSet` keys are **ephemerons** (a value is retained only while its key is reachable) and a `WeakRef` target is cleared when it becomes unreachable.
- The stack scan is conservative, so a stale stack word can delay clearing a weak target. The full GC description is in [Runtime deviations](../unimplemented/deviations.md) and [Unimplemented: toolchain](../unimplemented/toolchain.md).

## Iteration

- Any object exposing `[Symbol.iterator]()` is iterable in `for...of` and spread. `Array.prototype[Symbol.iterator]` is stable, and calling it returns an iterator object with `next()` that is itself iterable.
- Iteration is described further in [Codegen](./codegen.md#llvm-ir-code-generation-codegen) and [Symbol](./stdlib.md#symbol).
