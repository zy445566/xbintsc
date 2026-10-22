# Functions

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/functions.md)

| Feature | Parse | Semantics |
| --- | --- | --- |
| Default parameters `function f(a = 5)` | ✓ | ✓ implemented |
| Rest parameters `function f(...args)` | ✓ | ✓ implemented |
| Spread in call arguments `f(...args)` | ✓ | ✓ implemented |
| Destructuring parameters / bindings | ✓ | ✓ implemented |
| `arguments` object | ✓ (implicit) | ✓ implemented (arrow functions get their own parameters, not the outer function's `arguments`, unlike JS) |
| `this` binding / method call semantics | ✓ | ✓ implemented (`this` is threaded as the first ABI parameter; arrow functions inherit lexically) |
| `async` / `await` / Promise | ✓ | ✓ implemented with stackful-coroutine suspension: awaiting a pending promise pauses the body and the event loop resumes it (see the deviations table for the remaining differences) |
| `new.target` | ✗ | parsed, then rejected with `UnsupportedFeature` (no `new.target` semantics) |
| Generators / iterators / `yield` | ✓ implemented | `function*`, `yield`, `yield*`, `.next/.throw/.return`; `async function*`, `for await...of` and `Symbol.asyncIterator` are supported |
| Closure `arity` | — | `xt_closure_arity` is always -1, never filled in |
| `fn.call` / `fn.apply` / `fn.bind` | ✓ | ✓ implemented (the bound closure does not track partial-argument `length`) |
| `fn.name` / `fn.length` | ✓ | ✓ implemented (inferred from the declaration / assignment / property key; bound functions use `"bound ..."` and adjusted arity) |
| First-class built-in constructors (`const A = Array`, `new A(...)`, `typeof Map`) | ✓ | ✓ constructors are ordinary function values with `name`/`length`, a `prototype`, their statics (`A.isArray`) and the full `instanceof` protocol |
| First-class built-in methods (`typeof arr.map`, `const f = arr.push`, `obj.method?.()`) | ✓ | ✓ implemented as *unbound* method values: reading `arr.map` yields a function, `arr.map?.(cb)` and `obj.method?.()` bind the receiver as `this`, and a detached call (`const f = arr.map; f(cb)`) throws exactly like JavaScript. Method `name`/`length` are exposed |
| `fn.toString()` | ~ | returns a native-style placeholder (`function name() { [native code] }`) instead of the original source text |
