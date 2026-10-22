# Type system

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/types.md)

Type syntax is parsed into the AST and then **erased entirely** during binding /
code generation; no type checking is performed:

- Type annotations, return types, type parameters, type aliases, interfaces, generic constraints, etc.: parse ✓, check ✗.
- Checker diagnostic codes (`TypeMismatch` `NotCallable` `PropertyNotFound` `ArgumentCountMismatch`) are defined but **unused**.
- `as` / `satisfies` / non-null assertion `!`: erased directly, no assertion semantics.
- Generics: no runtime instantiation; type parameters are ignored.
- Optional chaining `?.` type narrowing: none (runtime short-circuit is implemented, but there is no type-level narrowing).
