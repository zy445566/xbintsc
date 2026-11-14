# Name binding and scopes (Binder)

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/binder.md)

Location: `src/binder/binder.ts`

- Scope kinds: module, function, block, `for`, `catch`
- Symbol kinds: `var` / `let` / `const` / `function` / `parameter` / `class` / `interface` / `type` / `enum` / `import` / `namespace`
- `var` and function declarations hoist to the function scope; `let` / `const` stay block-scoped. A `var` written inside a nested block or in a `for` head reuses that one hoisted symbol, so `{ var x = 1 }` is visible (and assignable) after the block
- A destructuring *parameter* pre-declares its names and then binds the default values nested inside the pattern, so `({ a = () => 1 }) => a()` gets a function of its own
- A `switch` body is one shared block scope: the declarations of every `case` / `default` clause are pre-declared before any clause is bound
- A `static { ... }` class block is bound as a hidden static function of the class (see [Classes](../unimplemented/classes.md))
- Identifier → declaration resolution; unresolved identifier collection (`CannotFindName`)
- Closure capture analysis: outer variables referenced by an inner function are marked `captured` / `boxed`, and capture indices are threaded through intermediate closures
- Parameters are registered as local symbols; the self-name of a named function expression is registered as `const`
- Type-only declarations (interface / type alias) do not participate in value capture
