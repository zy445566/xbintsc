# Semantic checker

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/checker.md)

Location: `src/checker/` (`checker.ts`, `undefined.ts`, `declarations.ts`,
`calls.ts`), invoked by `generate()` between binding and LLVM lowering.

The checker runs **by default**; `--no-check` (or `check: false` in the
programmatic API) turns it off. It reports only facts that need no type
inference, so every diagnostic it produces is a real error rather than a guess:

| Diagnostic | Code | What is checked |
| --- | --- | --- |
| `Cannot find name` | TS3002 | An identifier that resolves to no declaration and no runtime global |
| `Duplicate identifier` | TS3001 | Same-scope declarations that cannot merge (`let x` twice, `function f` then `const f`, `let` colliding with a parameter, ...) |
| `Argument count mismatch` | TS4004 | A direct call to a locally visible function or `const`-bound arrow/function expression with too few or too many arguments |

## What counts as a global

The known-global set is built from the same tables code generation uses
(`src/codegen/generator/tables.ts`): `console`, `Math`, `JSON`, `parseInt`,
timers, `fetch`, the constructors, typed arrays, Node namespaces such as
`Buffer`/`process`, and the globals registered by enabled extensions (`print`,
`__registerScript`, native manifests). A name added to a table becomes legal in
the checker in the same commit, so the two cannot drift apart.

Type-only positions are not value references: `x as Foo`, `x satisfies Foo` and
the type arguments of `f<Foo>()` are erased and never reported as unresolved.

Bundled sources under `node_modules` are not checked. The checker is a gate for
the code you wrote; a dependency may legitimately use a host global or a call
pattern the heuristic cannot see, and failing the build on its internals would
make the default-on checker unusable with packages.

## What is deliberately not checked

This is not a type checker. It performs no assignability, narrowing, generics
instantiation or flow analysis, and it does not validate the erased type
syntax:

- `const x: number = "hello"` still compiles; annotations are documentation.
- `as` / `satisfies` / non-null `!` remain runtime no-ops.
- `TypeMismatch` (TS4001), `NotCallable` (TS4002) and `PropertyNotFound`
  (TS4003) are still unused.
- Calls through a property (`object.method()`), an import, a `let`/`var`
  binding that is reassigned, or a spread argument are skipped: their signature
  is not a static fact.

## Arity details

The required count is the number of parameters before the first optional,
defaulted or rest parameter; `this:` parameters do not occupy an argument slot.
A rest parameter removes the upper bound. Overloaded/merged declarations are
skipped because no single signature is authoritative, and a function whose body
references `arguments` is exempt because its parameter list is not the whole
signature.

Two same-name imports are allowed: bundling merges every module into one scope,
and separate modules routinely import `readFileSync` or `dirname` from the same
builtin. A local declaration still cannot collide with an import.

## Behaviour without the checker

`--no-check` restores the previous permissive pipeline: type syntax is erased
and lowering continues, which is useful for compatibility with code that
intentionally calls functions with a different arity. It also restores the
silent `Duplicate identifier` behaviour: `let value = 1; let value = 2;`
compiles and the first value wins. Prefer keeping the checker on.

## Cache and diagnostics

`check` is part of the incremental cache key, so a `--no-check` build never
reuses a checked artifact (or vice versa). With the checker on, code generation
does not repeat its own `Cannot find name` error, so each name is reported
exactly once. `xbintsc check` runs the checker and stops before linking; it is
the fast gate for editors and pre-commit hooks.
