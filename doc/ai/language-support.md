# Language support: what compiles, what behaves differently

xbintsc compiles a **practical subset** of TypeScript. Use this page to decide
what you can write; follow the links for exact wording.

Canonical detail (always current, much longer):

- [../implemented.md](../implemented.md) — everything that works, by compiler stage
- [../unimplemented.md](../unimplemented.md) — unsupported syntax and deviations, one page per area
- [../implemented/node.md](../implemented/node.md) / [../unimplemented/node.md](../unimplemented/node.md) — the Node extension

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
| Top-level `await` | ⚠️ | works by blocking on the event loop until the promise settles (it does not yield the module body like an ES module); prefer wrapping it in an `async` function |

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
| `new.target` | 🚫 | parsed, then rejected with a clear `UnsupportedFeature` |
| `import.meta` | ✅ | `url`, `dirname`, `filename` (resolved from the executable path) |
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
| Generators `function*`, `yield`, `yield*` | ✅ | sync and `async function*`; `for await...of` and `Symbol.asyncIterator` |
| `arguments` object | ✅ | implicit; arrows see their own parameters |
| Argument-count validation | 🚫 | never checked, though `fn.length` reports declared arity |

## Async, Promises, event loop

| Feature | Status | Notes |
| --- | --- | --- |
| `async` / `await`, `Promise`, `Promise.all` | ✅ | real suspension: an `async` body runs on a stackful coroutine, and awaiting a **pending** promise pauses it until the promise settles. Every `await` yields once, so a settled value or non-promise is queued as a microtask like Node |
| `setTimeout` and callbacks | ✅ | timers run on the core event loop; `await new Promise(r => setTimeout(r, ms))` works |
| `queueMicrotask` / `setImmediate` / `performance.now` | ✅ | microtasks share the promise FIFO queue; `setImmediate` is a zero-delay timer with numeric ids; `performance.now` is monotonic fractional milliseconds |
| Sockets / servers (`http`, `net`, `dgram`) | ✅ | callbacks run on the event loop and can settle a promise you await (`net` / `http` examples included in the tests) |
| `fs/promises` | ✅ | file, directory and `FileHandle` operations run on the async I/O worker pool and settle on the reactor, so timers and sockets keep running while they wait |
| Global `fetch` | ✅ | HTTP/1.1 client whose connect/send/receive runs on the same worker pool; `await fetch(...)` yields and redirects are followed on the reactor. `http://` only (no TLS) and the body is buffered before resolving |
| Worker threads | 🚫 | workers are child processes |
| `async` generators | ✅ | suspend at `await` and `yield`; every `.next()` returns a promise; `for await...of` supported |

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

Implemented: `Math`, `JSON`, `Date`, `RegExp`, `Map`, `Set`, `WeakMap`/`WeakSet`/`WeakRef`,
`Symbol`, `Error` family, `BigInt`, `Array`/`String`/`Number`/`Object` methods,
typed arrays, `String.prototype.match`/`split`/`replace` with capture groups,
immutable array helpers (`toReversed`, `toSorted`, `toSpliced`, `with`), global
URI functions, first-class built-in constructors (`const A = Array; new A(...)`,
`A.isArray`, `x instanceof A`) and `Object.prototype`
(`Object.getPrototypeOf({}) === Object.prototype`).

| Missing or different | Status |
| --- | --- |
| `String.prototype.normalize` | 🚫 |
| `structuredClone` | ✅ arrays, objects, Map, Set, Date, RegExp and typed arrays |
| `Error.stack` capture | ⚠️ function-level frames (name/file/line), no columns; generator bodies are omitted |
| Global RegExp `lastIndex` | ⚠️ `test`/`exec` ignore it for `/g` and `/y` |
| Sparse array holes, array out-of-bounds | ⚠️ holes are not tracked distinctly; out-of-bounds reads give `undefined` |

See [Unimplemented: standard library](../unimplemented/stdlib.md) for the full list.

## Modules

| Feature | Status | Notes |
| --- | --- | --- |
| ESM `import` / `export` | ✅ | |
| Relative multi-file bundling | ✅ | a `./helper.js` specifier resolves to `helper.ts` |
| `import * as ns`, default and named imports | ✅ | |
| Node built-in modules | ✅ | only with `--ext node` — see [extensions.md](./extensions.md) |
| Bare third-party npm packages | ⚠️ | resolved from `node_modules` and bundled as source (ESM and CommonJS); no runtime lookup, circular dependencies rejected |
| `require()` / CommonJS | ⚠️ | rejected in your own source with a hint to use `import`; CommonJS packages inside `node_modules` are bundled |
| Circular dependencies | 🚫 | value imports only; `import type` / `export type` are erased and never form an edge |
| Live bindings | ⚠️ | imports and namespace members are snapshots |
| `__dirname`, `__filename` | 🚫 | use `import.meta.dirname` / `import.meta.filename` |

## Strings, numbers, memory

| Item | Behaviour |
| --- | --- |
| `String.prototype.length` | ✅ UTF-16 code units (`"中文😀".length === 4`); indexing, `slice`, `indexOf`, `split`, `padStart` and `codePointAt` use the same model. Storage stays UTF-8 |
| RegExp Unicode matching | ⚠️ the engine is byte-oriented: a literal `中` still matches, but `.` and character classes consume one byte per repetition, and `\d`/`\w` stay ASCII. Match offsets (`match.index`, `search`, replacement callbacks) are reported in UTF-16 code units |
| Number formatting and coercion | ✅ matches Node (`toFixed`, `toPrecision`, hex/octal/binary parsing) |
| `BigInt` | ✅ implemented |
| Garbage collection | ✅ non-moving mark-sweep; explicit roots plus a conservative C-stack scan; single-threaded and stop-the-world. WeakMap/WeakSet keys use an ephemeron fixed point and WeakRef targets are cleared when unreachable; a stale conservative stack word can delay clearing |
| Threads | 🚫 single-threaded; `worker_threads` exists in the Node extension only |

## Platform

Builds target macOS, Linux and Windows on x64 and arm64. On Windows the target
is the **MSVC ABI**, so the compiler must be an MSVC-targeting clang (the LLVM
bundled with Visual Studio works) — see
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
