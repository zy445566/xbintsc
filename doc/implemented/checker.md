# Semantic checker

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/checker.md)

Location: `src/checker/` (`checker.ts`, `undefined.ts`, `declarations.ts`,
`calls.ts`, `type-system.ts`, `assignability.ts`, `flow.ts`, `typecheck.ts`),
invoked by `generate()` between binding and LLVM lowering.

The checker runs **by default**; `--no-check` (or `check: false` in the
programmatic API) turns it off. It combines inference-free checks with a small
structural type model:

| Diagnostic | Code | What is checked |
| --- | --- | --- |
| `Cannot find name` | TS3002 | An identifier that resolves to no declaration and no runtime global |
| `Duplicate identifier` | TS3001 | Same-scope declarations that cannot merge (`let x` twice, `function f` then `const f`, `let` colliding with a parameter, ...) |
| `Argument count mismatch` | TS4004 | A direct call to a locally visible function or `const`-bound arrow/function expression with too few or too many arguments |
| `Property not found` | TS4003 | A property read on a shape that is statically known |
| `Type mismatch` | TS4001 | An initializer, assignment, return value or call argument that is definitely not assignable |
| `Not callable` | TS4002 | Calling a value whose type is a primitive (number, string, boolean, ...) |

## What the type model knows

The checker resolves the type syntax it can understand and treats everything
else as `any`:

- keyword types, literal types, `A | B` unions and object type literals;
- interfaces and type aliases (including generic aliases and `extends`);
- classes: instance fields, methods, accessors, parameter properties,
  `this.x = ...` members and local ancestor classes. Static members are not
  instance members;
- arrays, tuples, function types, object literals and `new LocalClass()`;
- simple generics: a type parameter is `any` at its declaration, and a call
  infers it from an argument (`T`, `T[]`, `Array<T>`) or takes it from an
  explicit type argument list.

Anything the model cannot name — mapped/conditional types, `keyof`,
`typeof` queries, indexed access, utility types, imported type names that
bundling renamed, an unknown base class — becomes `any`. `any` is compatible
with everything, which is the central safety property: unsupported syntax can
hide a bug, but it cannot reject working code.

## Assignability

Checked at variable initializers, `=` assignments, `return` statements and call
arguments:

- primitives and literal types;
- object/interface/class shapes: every required target property must exist and
  be assignable; extra source properties are allowed;
- arrays and tuples element-wise;
- functions: parameter count and return type (conservatively);
- unions: every source member must fit a target member, or some target member
  must accept the source.

`let`/`var` infer widened types (`let x = 1` is `number`), `const` keeps the
literal. `null` and `undefined` are accepted wherever a value is expected
(there is no strict-null mode). Functional return types are not inferred: an
unannotated function returns `any`, so its callers are never rejected.

## Property existence

Shapes come from an explicit annotation or from a `const` initialized with an
object literal / `new LocalClass(...)`. Optional properties, `Object.prototype`
members (`toString`, `hasOwnProperty`, ...), index signatures and inherited
class members are all accepted.

A shape becomes untrusted — and its variable is left alone — when it can change
after the declaration: a property write/delete/update, a call that may mutate
it (`mutate(object)`, `Object.assign(object, ...)`), aliasing (`const p =
object`), a dynamic constructor (`Object.assign(this, ...)`, `this[key] = ...`)
or prototype augmentation (`Object.assign(C.prototype, ...)`). Writes to a
missing property are allowed (xbintsc programs legitimately grow objects after
their literal); the write opens the shape instead of reporting.

Not checked: `object["x"]` element access, destructuring, shapes reached
through a function return, or aliasing through array/object containers.

## Narrowing

Inside a branch the checker refines a variable when the condition is one of:

- `value !== null` / `!== undefined` / `!= null` (and the `===` forms);
- `typeof value === "string" | "number" | "boolean" | "bigint" | "symbol" | "undefined"`;
- `value instanceof LocalClass`;
- truthiness (`if (value)`, `value &&`, `value ||`);
- combinations through `&&`, `||`, `??` and `!`.

`if`/`else` and conditional expressions (`cond ? a : b`) apply the refinement;
other control flow does not.

## What is deliberately not checked

- Full inference: unannotated function returns, complex generic constraints and
  higher-order inference are treated as `any`.
- Excess-property checks on fresh object literals.
- `as`/`satisfies` assertions may narrow or widen freely (they are erased).
- Overload resolution, mapped types, conditional types and template-literal
  types are not modeled.
- Bundled sources under `node_modules` are not checked at all: the checker is a
  gate for the code you wrote, and failing on a dependency's internals would
  make the default-on checker unusable with packages.

## Behaviour without the checker

`--no-check` restores the previous permissive pipeline: type syntax is erased
and lowering continues. It also restores the silent `Duplicate identifier`
behaviour (`let value = 1; let value = 2;` compiles and the first value wins).
Prefer keeping the checker on.

## Cache and diagnostics

`check` is part of the incremental cache key, so a `--no-check` build never
reuses a checked artifact (or vice versa). With the checker on, code generation
does not repeat its own `Cannot find name` error, so each name is reported
exactly once. `xbintsc check` runs the checker and stops before linking; it is
the fast gate for editors and pre-commit hooks.
