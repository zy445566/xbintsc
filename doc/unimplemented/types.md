# Type system

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/types.md)

Type syntax is **not** erased blindly any more: the default
[semantic checker](../implemented/checker.md) resolves the subset of TypeScript
types it can model and checks assignability, property existence and simple flow
narrowing. Type syntax is still erased during code generation, and the checker
is deliberately incomplete:

- **No full inference.** An unannotated function returns `any`; generic
  constraints, higher-order inference and return-type inference are not
  attempted.
- **No strict-null mode.** `null` / `undefined` are assignable wherever a value
  is expected.
- **No excess-property check** on fresh object literals.
- **No overload resolution, mapped types, conditional types or template-literal
  types.** They resolve to `any`.
- **`as` / `satisfies` / non-null `!`** are still erased and do not validate
  anything; the checker treats them as assertions.
- **Complex generics.** Simple type parameters are inferred from arguments
  (`T`, `T[]`, `Array<T>`); mapped/conditional generic machinery is not.
- **Type arguments are not checked for arity**; an unknown type parameter is
  just `any`.

All six checker diagnostic codes are now used: `DuplicateIdentifier` (TS3001),
`CannotFindName` (TS3002), `TypeMismatch` (TS4001), `NotCallable` (TS4002),
`PropertyNotFound` (TS4003) and `ArgumentCountMismatch` (TS4004).
