# LLVM IR code generation (Codegen)

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/codegen.md)

Location: `src/codegen/llvm.ts`

- Emits LLVM IR text (`.ll`); no custom register allocation (relies on `alloca` + mem2reg).
- Statement / block boundary values live in `alloca`; conditionals and short-circuits materialize into temporary slots instead of `phi`.
- Property reads are inline-cached per source site: each access owns a zeroed `[2 x i64]` global (`@ic.N`) passed to `xt_get_ic`. Ordinary objects carry an interned hidden-class shape (`xt_shape`, a `(parent, key)` transition tree) and the cache stores `(shape, property index)`, so a hit is a shape-pointer compare plus one key check instead of a linear property scan. Inserts/deletes and integer-key reordering rebuild or transition the shape, which invalidates stale caches; arrays, functions and containers fall back to `xt_get`.
- Numeric operators keep the inline two-double fast path; operands that are numeric literals skip their tag check. Conditions whose expression is a comparison, `in`, `instanceof` or `!` are already boolean tags, so codegen emits `icmp eq i64 value, XT_TRUE` instead of calling `xt_truthy` (this removes the call from the hot loop condition).
- Control flow: `if` / `while` / `do` / `for` / `for...of` / `for...in`, `switch`, `try/catch/finally`, `break` / `continue` / `return`.
  - `for...of` and spread iterate arrays, strings, typed arrays, `Map`, `Set`, generators and any object exposing `[Symbol.iterator]()` through `xt_iter_open` → `xt_iter_has` / `xt_iter_value` (`Map` yields `[key, value]` pairs); a non-iterable throws `TypeError`. Arrays also expose a stable `Array.prototype[Symbol.iterator]`; calling it returns an iterator object with `next()` that is itself iterable.
  - `switch` tests each `case` with strict equality, executes on a hit, and falls through until `break`.
  - `try/catch/finally` is implemented with a runtime `_setjmp` frame: `xt_try_enter` pushes, `_setjmp` catches, `xt_throw` long-jumps. The IR passes the caller's frame address (`@llvm.frameaddress(0)`) as the second `_setjmp` argument, matching clang's MSVC lowering: the Windows UCRT `_setjmp` stores that frame in `_JUMP_BUFFER.Frame` and `longjmp` feeds it to `RtlUnwind`, so omitting it made `longjmp` unwind to a bogus target (`STATUS_BAD_FUNCTION_TABLE`). `_setjmp` is used rather than the exported `setjmp` symbol, whose Windows ABI is an incompatible two-argument routine. Functions containing `try` force local variables to stay in memory (inline-asm escape points) so values survive a long jump.
  - `for...in` reuses `xt_object_keys` to enumerate keys (arrays / strings yield string indices).
- Expressions:
  - Identifiers, numbers, BigInt (arbitrary precision), strings, templates, booleans, `null`, `undefined`, `arguments`
  - Arithmetic / comparison / logical / short-circuit / conditional / bitwise / unary (including `typeof` `void`) / prefix-postfix increment-decrement / compound assignment / logical assignment
  - Array literals (including spread `[...]`), object literals (including shorthand / methods / object spread `{...obj}`)
  - Typed arrays, `ArrayBuffer`, `DataView`, `TextEncoder` / `TextDecoder`: see [Typed arrays and binary data](./typed-arrays.md) and [Strings and text](./strings.md)
  - Property access (with a `length` special case, `Math` constants), element access, calls
  - First-class built-in constructors (`const A = Array`, `const M = Map`, `const E = TypeError`, typed arrays): a bare reference lowers to `xt_builtin_constructor`, giving a cached function with `name` / `length`, a `prototype`, and its statics (`A.isArray`, `A.from`, `M.prototype`, ...). Calling or `new`-ing an alias dispatches to the same runtime constructor, and `x instanceof A` walks the shared prototype chain.
  - Optional chaining `?.` / `?.[]` / `?.()`: nullish check short-circuits to `undefined`
  - `delete`, `in`, `instanceof`
  - `this` (saved to the function's `%saved.this`; arrow functions read from an environment slot), `super` (`super.x` / `super(...)`), `new`, `await`
  - Destructuring: a binding-pattern default (`const { a = f() } = o`) is emitted as a branch, so `f()` runs only when the property is `undefined` — never eagerly
  - Closures (arrow functions / function expressions / function declarations) and capture environment construction: a capture is the symbol's *box*, so writes inside the closure reach the enclosing binding. A declared function called by name builds its environment at the call site (`fn.captures` / `fn.capturesThis` disable the direct-call fast path); otherwise it would run with a null environment and read every free variable as `undefined`.
- Classes: constructor closure + prototype object, stored in an LLVM global (`@class.<id>`); instance fields are initialized before the constructor body; `static` members live in the constructor's property bag; `extends` sets up the prototype chain; `super(...)` is invoked through a hidden `__ctor` on the *parent* prototype. The parent is derived from the class being defined (`getPrototype(getPrototype(@class.N))`), never from the receiver: walking `this` resolves to the wrong class once an instance of a subclass calls a base constructor, which recurses forever in a chain deeper than two levels.
- `async` / `async function*`: closures are marked with `xt_function_set_async` / `xt_function_set_async_generator` and run on stackful coroutines — see [Async, promises and generators](./async.md).
- Standard library calls dispatch through `xt_call_method` / `xt_math_call` / `xt_object_*` and the per-constructor `xt_*` entry points; the surface is listed in [Standard library](./stdlib.md).
- Global string pool (`@.str.N` private constants, UTF-8 escaped).
- Builtin calls: `console.log` / `info` / `warn` / `error`, `Math.*`, `Object.*`, array / string methods, global functions, extension builtins (uniform `(argc, argv)` ABI).
- `main` entry point (returns 0, calls the module function, then drains microtasks and runs the event loop until no timers or descriptors remain).
- Unsupported nodes uniformly report `UnsupportedFeature`; they never crash.
