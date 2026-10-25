# Parser

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/parser.md)

Location: `src/parser/parser.ts`, `src/ast/nodes.ts`

Module syntax (`import` / `export`, import attributes) and the driver-level bundling that gives it runtime meaning are covered in [Module system](./modules.md).

## Statements

- Variable declarations: `var` / `let` / `const`, with multiple declarators `const a = 1, b = 2;`
- Function declarations, function expressions and methods, including the `async` modifier, generators (`function*`, `yield`, `yield*`, `.next`/`.throw`/`.return`) and async generators (`async function*`, `for await...of`)
- `class` declarations / class expressions (constructor, fields, methods, `static`, `static { ... }` blocks, `extends`)
- `if` / `else`
- `while`, `do...while`
- `for` (init, condition and increment may all be omitted)
- `for...of`, `for...in`
- `return`, `break`, `continue`, `throw` (including labeled `break label` / `continue label`)
- `switch` / `case` / `default` (including fall-through)
- `try` / `catch` / `finally` (catchable exceptions based on a runtime setjmp frame)
- `export var` / `export let` / `export const` (modifier parsed then erased)
- Block statement `{}`, empty statement `;`, `debugger;`
- Expression statements

## Expressions

- All common operators with precedence / associativity (see [Operators](./operators.md))
- Assignment expressions and all compound assignments (see [Operators](./operators.md))
- Conditional (ternary) expressions `a ? b : c`
- Comma / sequence expressions `a, b, c` (in `for` headers, parenthesized groups and expression statements)
- Arrow functions `() => expr` / `() => { ... }` (including type parameters and return type annotations)
- Function expressions `function () {}` and named function expressions `function g() {}` (also `async function` expressions)
- Call expressions `f(...)`, member access `a.b`, element access `a[i]`
- Array literals `[1, 2]`, sparse array elision, spread `[...a]` (also over strings, `Map` and `Set`)
- Object literals `{ a: 1 }`, shorthand properties `{ a }`, method shorthand `{ m() {} }`, getter / setter shorthand `{ get x() {} }` / `{ set x(v) {} }`, computed keys `{ [expr]: 1 }`, object spread `{ ...obj }`
- Template literal `${}` substitutions, tagged templates (with raw strings and `String.raw`)
- Parenthesized expressions, `as` / `satisfies` / non-null assertion `!` (type erasure)
- Unary: `+ - ! ~ typeof void`, prefix / postfix `++ --`
- Optional chaining `?.` / `?.[]` / `?.()` (with nullish short-circuit semantics)
- `delete` expressions (delete an object property)

## TypeScript type syntax (parsed only, structure preserved then erased)

- Type annotations, return type annotations, type parameters `<T>` and constraints `<T extends U>`, type parameter defaults
- Type references, qualified names `A.B`
- Unions `|`, intersections `&`, arrays `T[]`, tuples `[T, U]`, optional tuple members `T?`, rest tuple members `...T`
- Function types `(a: T) => U`, construct signatures `new () => T`
- Object type literals, property signatures, method signatures, index signatures `[k: string]: T`
- Conditional types `T extends U ? X : Y`, mapped types `{ [K in T]: U }`, `infer`
- Type operators `keyof`, `unique`, `readonly`
- `typeof` (type query), indexed access types `T[K]`
- Literal types, `this` types
- Type predicates `x is T` / `asserts x is T`
- Interfaces, type aliases, enums, namespace / module declarations
- The various forms of `import` / `export` (structural parsing)

## ASI

- Automatic Semicolon Insertion, based on `precededByLineBreak` / `}` / EOF.
