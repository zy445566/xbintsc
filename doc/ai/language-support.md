# Language support: what compiles, what behaves differently

xbintsc compiles a **practical subset** of TypeScript. Use this page to decide
what you can write; follow the links for exact wording.

Canonical detail (always current, much longer):

- [../implemented.md](../implemented.md) — everything that works, by compiler stage
- [../unimplemented.md](../unimplemented.md) — unsupported syntax, deviations, quick reference
- [../node-implemented.md](../node-implemented.md) / [../node-unimplemented.md](../node-unimplemented.md) — the Node extension

> Chinese: [../zh-CN/implemented.md](../zh-CN/implemented.md), [../zh-CN/unimplemented.md](../zh-CN/unimplemented.md)

## Legend

| Mark | Meaning |
| --- | --- |
| ✅ | Works as in Node/TypeScript |
| ⚠️ | Works but deviates from the standard |
| 🚫 | Rejected — parse error or `UnsupportedFeature` |
| — | Parsed and erased, with no runtime effect |

## Statements and declarations

| Feature | Status | Notes |
| --- | --- | --- |
| `var` / `let` / `const`, blocks, `if`, `for`, `for...of`, `for...in`, `while`, `do...while` | ✅ | `for...in` enumerates own keys only (no prototype chain) |
| `switch`, `break`, `continue`, labels | ✅ | labeled `break`/`continue` included |
| `try` / `catch` / `finally`, `throw` | ✅ | `finally` runs on early `return`/`break`/`continue` |
| `function`, arrow functions, default/rest parameters | ✅ | |
| `return`, `throw` as expressions' operands | ✅ | |
| `namespace` / `module` declaration | 🚫 | parse ✓, codegen ✗ |
| `with`, `debugger` | 🚫 | not supported |
| Top-level `await` | 🚫 | wrap it in an `async` function |

## Expressions and operators

| Feature | Status | Notes |
| --- | --- | --- |
| Arithmetic, bitwise, logical, comparison, assignment operators | ✅ | ES `ToPrimitive` / `+` coercion match Node |
| `===` / `!==` / `==` / `!=` | ✅ | loose equality matches Node |
| Optional chaining `?.`, nullish `??`, logical assignment `??=` | ✅ | whole-chain short-circuit implemented |
| Template literals, tagged templates | ✅ | tagged templates have `raw` + `String.raw` |
| Spread / rest in calls, arrays and object literals | ✅ | |
| Destructuring (bindings, parameters, nested) | ✅ | defaults + rest supported |
| `delete`, `in`, `instanceof`, `typeof` | ✅ | |
| Comma operator, `void` | ✅ | |
| `new.target` | 🚫 | not implemented |
| `import.meta` | 🚫 | parsed but has no value |
| `super` | ⚠️ | single-level inheritance correct; depth > 1 may be inaccurate |

## Functions, classes, objects

| Feature | Status | Notes |
| --- | --- | --- |
| Closures (capture by reference through boxes) | ✅ | one ABI for direct and closure calls |
| `this`, method calls, arrow lexical `this` | ✅ | arrow functions get their own `arguments`, unlike JS |
| `call` / `apply` / `bind`, `fn.name` / `fn.length` | ✅ | a bound closure does not track partial-argument `length` |
| First-class built-in methods (`arr.map` as a value) | ✅ | exposed as *unbound* method values; a detached call throws like JS |
| `fn.toString()` | ⚠️ | returns `function name() { [native code] }`, not the source text |
| Classes: fields, methods, statics, getters/setters, `extends`/`super`, `instanceof` | ✅ | |
| Constructor parameter properties `constructor(public x: T)` | ✅ | |
| `#private` fields / methods / statics | ⚠️ | stored under a literal `#x` key; no access enforcement; parent/child name collisions may alias |
| `enum` / `const enum` | ✅ | forward + reverse mapping |
| `private` / `protected` / `public` / `readonly` / `abstract` / `implements` | — | erased, no access control |
| Generators `function*`, `yield`, `yield*` | ✅ | `async` generators are not supported |
| `arguments` object | ✅ | implicit; arrows see their own parameters |
| Argument-count validation | 🚫 | never checked, though `fn.length` reports declared arity |

## Async, Promises, event loop

| Feature | Status | Notes |
| --- | --- | --- |
| `async` / `await`, `Promise`, `Promise.all` | ⚠️ | **synchronous microtask model** — `await` on a settled promise continues synchronously |
| `setTimeout` and callbacks | ⚠️ | the event loop runs **after** the program body, so a promise settled from a timer cannot be awaited |
| Sockets / servers (`http`, `net`, `dgram`) | ⚠️ | same model: callbacks run on the event loop after the program body |
| A real async event loop, worker threads | 🚫 | not implemented |

## Types: parsed, then erased

**No type checking happens.** Type syntax is parsed into the AST and erased
during binding/codegen:

| Construct | Status |
| --- | --- |
| Type annotations, return types, type aliases, interfaces | — parsed, erased, never checked |
| Generic type parameters and constraints | — no runtime instantiation |
| `as`, `satisfies`, non-null `!` | — erased, no assertion semantics |
| Optional-chaining type narrowing | 🚫 |
| Diagnostics `TS4001`–`TS4004` (`TypeMismatch`, `NotCallable`, `PropertyNotFound`, `ArgumentCountMismatch`) | 🚫 defined but never emitted |

A type error in your source is therefore not a compile error — only runtime
behaviour is checked. Write the runtime code you mean.

## Standard library

Implemented: `Math`, `JSON`, `Date`, `RegExp`, `Map`, `Set`, `WeakMap`/`WeakSet`,
`Symbol`, `Error` family, `BigInt`, `Array`/`String`/`Number`/`Object` methods,
typed arrays, `String.prototype.match`/`split`/`replace` with capture groups,
immutable array helpers (`toReversed`, `toSorted`, `toSpliced`, `with`), global
URI functions.

| Missing or different | Status |
| --- | --- |
| `String.prototype.normalize`, `structuredClone` | 🚫 |
| `Error.stack` capture | 🚫 (not captured) |
| `Object.getPrototypeOf({})` | ⚠️ returns `undefined`, not `Object.prototype` |
| Global RegExp `lastIndex` | ⚠️ `test`/`exec` ignore it for `/g` and `/y` |
| Sparse array holes, array out-of-bounds | ⚠️ holes are not tracked distinctly; out-of-bounds reads give `undefined` |

See section 3 of [../unimplemented.md](../unimplemented.md) for the full list.

## Modules

| Feature | Status | Notes |
| --- | --- | --- |
| ESM `import` / `export` | ✅ | |
| Relative multi-file bundling | ✅ | a `./helper.js` specifier resolves to `helper.ts` |
| `import * as ns`, default and named imports | ✅ | |
| Node built-in modules | ✅ | only with `--ext node` — see [extensions.md](./extensions.md) |
| Bare third-party npm packages | 🚫 | `node_modules` ESM packages are bundled as source only when no extension claims the specifier |
| `require()` / CommonJS | 🚫 | rejected with a hint to use `import` |
| Circular dependencies | 🚫 | |
| Live bindings | ⚠️ | imports and namespace members are snapshots |
| `import.meta`, `__dirname`, `__filename` | 🚫 | |

## Strings, numbers, memory

| Item | Behaviour |
| --- | --- |
| `String.prototype.length` | ⚠️ **UTF-8 bytes**, not UTF-16 units: `"é".length === 1`, `"😀".length === 4`; `codePointAt` differs accordingly |
| Number formatting and coercion | ✅ matches Node (`toFixed`, `toPrecision`, hex/octal/binary parsing) |
| `BigInt` | ✅ implemented |
| Garbage collection | ✅ non-moving mark-sweep; explicit roots plus a conservative C-stack scan; single-threaded and stop-the-world; no weak references |
| Threads | 🚫 single-threaded; `worker_threads` exists in the Node extension only |

## Platform

Builds target macOS, Linux and Windows on x64 and arm64. On Windows the target
is the **MSVC ABI**, so clang needs the MSVC/SDK environment — see
[../requirements.md](../requirements.md). clang **16 or newer** is required.

## Before you write a large program

1. Skim the table for the features you plan to use; open
   [../unimplemented.md](../unimplemented.md) for anything marked ⚠️ or 🚫.
2. Prototype with `xbintsc emit app.ts` — the cheapest way to learn whether a
   construct is supported, since it needs no clang.
3. Run the real thing with `xbintsc run app.ts`. If a build fails, go to
   [troubleshooting.md](./troubleshooting.md).
4. When exact Node behaviour matters, run the same program under Node and
   compare output; the project's own test suite does this differential check.
