# Type system

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/types.md)

Type syntax is parsed into the AST and then **erased** during binding / code
generation. There is no assignability check: a type annotation is documentation,
not a constraint. The default [semantic checker](../implemented/checker.md) only
covers facts that need no inference:

- Type annotations, return types, type parameters, type aliases, interfaces,
  generic constraints, etc.: parse ✓, type check ✗.
- Checker diagnostics actually emitted: `DuplicateIdentifier` (TS3001),
  `CannotFindName` (TS3002) and `ArgumentCountMismatch` (TS4004).
  `TypeMismatch` (TS4001), `NotCallable` (TS4002) and `PropertyNotFound`
  (TS4003) are defined but **unused**.
- `as` / `satisfies` / non-null assertion `!`: erased directly, no assertion semantics.
- Generics: no runtime instantiation; type parameters are ignored.
- Optional chaining `?.` type narrowing: none (runtime short-circuit is implemented, but there is no type-level narrowing).
