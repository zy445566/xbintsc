# Expressions and operators

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/expressions.md)

## Parsed but codegen reports `UnsupportedFeature`

None — generators are implemented.

> Tagged templates are implemented, including raw strings and `String.raw`.
> Generators (`function*`, `yield`, `yield*`) are implemented with stackful
> coroutines, including `next`/`throw`/`return`, `for...of`, spread and
> delegation. `async function*` and `for await...of` are implemented: the body
> may suspend at both `await` and `yield`, and every `.next()` returns a
> promise. `import.meta` exposes `url`, `dirname` and `filename`, resolved from
> the running executable's path (see [Module system](./modules.md)).

## Not supported by the parser

None known at the expression level.

## Unimplemented operators

| Operator | Status | Notes |
| --- | --- | --- |
| `new.target` | ✗ | parsed, then rejected with `UnsupportedFeature` (no `new.target` semantics) |
| `typeof` / `void` / `in` / `delete` / `instanceof` | ✓ | mapped to runtime helpers (`xt_typeof`, `xt_in`, `xt_delete`, `xt_instance_of`) |
