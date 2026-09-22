# xbintsc Unimplemented Syntax and Features

This document was compiled by checking the source (`src/`, `runtime/`) and tests
(`tests/`) file by file, and lists the syntax and features that are
**currently unimplemented / only partially implemented / semantically different
from the standard**.

Criteria (ordered by severity):

1. **Not parsed**: the parser reports a syntax error immediately; code generation is never reached.
2. **Parsed but codegen errors**: the AST / binding can be built, but `codegen` reports `UnsupportedFeature`.
3. **Parsed and compilable, but semantics are missing or differ**: a binary is produced, but the runtime result does not match ECMAScript / TypeScript semantics.

> Language: **English** | [简体中文](./zh-CN/unimplemented.md)

> Recently completed: **ECMAScript semantics + differential testing** — number
> formatting / coercion (`toFixed` / `toPrecision` / `toExponential`, hex / octal
> / binary parsing, full `+` and relational `ToPrimitive`), `JSON.stringify`
> omission rules, default `Array.prototype.sort` (stable, string order),
> insertion-ordered integer keys, whole-chain optional chaining, `finally` on
> early `return` / `break` / `continue`, object-key insertion order, UTF-8-aware
> `console` inspection with circular references, `Array.prototype.splice`,
> `String.prototype.match` / `lastIndexOf` / `split` with limit, RegExp capture
> groups (`exec` / `match` / `replace` / `split` / `search`), immutable array
> methods (`toReversed` / `toSorted` / `toSpliced` / `with`),
> `Object.getOwnPropertyNames` / `groupBy`, global URI functions, labeled
> statements, `#private` fields / methods / statics, tagged templates (with raw
> strings and `String.raw`), and `import * as ns` for relative modules. The test
> suite now includes a **differential harness** that runs every case through both
> xbintsc and Node and compares output byte-for-byte.

> Also completed: `class` declarations / class expressions, `new` / `this`,
> inheritance `extends` / `super`, `instanceof`, methods / static members /
> instance fields / getters and setters / constructor parameter properties,
> `async` / `await` + `Promise`, multi-file `import` / `export` bundling
> (including `.js` → `.ts` specifier resolution and namespace imports), `Map` /
> `Set` / `Date` / `RegExp` / `JSON` / `BigInt`, destructuring bindings and
> parameters, `enum` / `const enum`, and **self-hosting** (the emitted LLVM IR
> reaches a byte-identical fixpoint).

---

## 1. Unimplemented at the statement level

### 1.1 Parsed but codegen reports `UnsupportedFeature`

| Syntax | Status | Notes |
| --- | --- | --- |
| `namespace` / `module` declarations | parse ✓, codegen ✗ | → "does not yet support this statement (module declaration)" |

> Labeled statements (`label: statement`, `break label`, `continue label`) are
> implemented, including labels on non-loop statements.

### 1.2 Not supported by the parser (immediate syntax error)

None known at the statement level. The parser is intentionally permissive and
accepts most TypeScript statement syntax; unsupported forms are rejected later
by code generation (see above).

---

## 2. Unimplemented at the expression level

### 2.1 Parsed but codegen reports `UnsupportedFeature`

| Syntax | Status | Notes |
| --- | --- | --- |
| `yield` expressions (generators) | parse ✓, codegen ✗ | → "does not yet support this expression (yield expression)" |

> Tagged templates are implemented, including raw strings and `String.raw`.
> `import.meta` is parsed but has no runtime value.

### 2.2 Not supported by the parser

None known at the expression level.

### 2.3 Unimplemented operators

| Operator | Status | Notes |
| --- | --- | --- |
| `new.target` | ✗ | not implemented |
| `typeof` / `void` / `in` / `delete` / `instanceof` | ✓ | mapped to runtime helpers (`xt_typeof`, `xt_in`, `xt_delete`, `xt_instance_of`) |

---

## 3. Unimplemented standard library

### 3.1 Implemented (summary)

- Arrays: `push` `pop` `shift` `unshift` `join` `slice` `splice` `indexOf` `lastIndexOf` `includes` `concat` `reverse` `forEach` `map` `filter` `reduce` `reduceRight` `find` `findIndex` `findLast` `findLastIndex` `some` `every` `sort` `flat` `flatMap` `fill` `copyWithin` `at` `keys` `values` `entries` and the immutable `toReversed` / `toSorted` / `toSpliced` / `with`
- Strings: `charAt` `charCodeAt` `codePointAt` `indexOf` `lastIndexOf` `includes` `startsWith` `endsWith` `slice` `substring` `substr` `split` `match` `replace` `replaceAll` `search` `toUpperCase` `toLowerCase` `trim` `trimStart` `trimEnd` `padStart` `padEnd` `repeat` `concat` `at` `localeCompare` `valueOf`
- `Math`: full set of functions and constants
- `Object.keys` / `values` / `entries` / `assign` / `getOwnPropertyNames` / `groupBy`, object spread `{...obj}`
- Global functions: `parseInt` `parseFloat` `isNaN` `isFinite` `Number` `String` `Boolean` `encodeURI` `decodeURI` `encodeURIComponent` `decodeURIComponent`
- `console.log` / `info` / `warn` / `error` / `dir` / `trace` / `assert` / `count` / `group` / `table` / `time`

### 3.2 Completed

- `JSON.parse` / `JSON.stringify`
- `Date`, `RegExp` (POSIX ERE subset with capture groups), `Map`, `Set`, `Promise`, `BigInt`
- `Array` statics (`isArray/of/from`), `Object` statics, `Number` statics, `String` statics (`fromCharCode` / `fromCodePoint` / `raw`)
- `Error` (`new Error(...)`, `extends Error`)

### 3.3 Still unimplemented

| Category | Status |
| --- | --- |
| `Symbol` constructor and symbol primitives | ✗ not implemented |
| `String.prototype.normalize` | ✗ not implemented |
| `structuredClone` | ✗ not implemented |
| Built-in error subclasses (`TypeError`, `RangeError`, …) | ✗ only `Error` exists; runtime throws use it internally |
| `AggregateError` constructor | ✗ `Promise.any` exists but rejects with a string instead of an `AggregateError` object |
| Iterator protocol / `Symbol.iterator` / custom `for...of` iterables | partial: arrays, strings, `Map` and `Set` are iterable in `for...of` / spread; a user-defined `Symbol.iterator` is not consulted |
| Generators / async iteration | ✗ not implemented |
| Timers / I/O / process and other host APIs | only via extensions (e.g. Node `fs`) |

---

## 4. Module system

| Feature | Status |
| --- | --- |
| Multi-file / module resolution and linking | ✓ driver-layer AST bundling (`src/driver/modules.ts`): parse each module, rename top-level symbols by module prefix, rewrite references, merge into one file and rebind |
| Named import/export | ✓ `import { a, b as c }` / `export { a as b }` / `export const/let/var/function/class` |
| Default import/export | ✓ `export default` / `import d from` |
| Re-export `export { x } from` / `export * from` | ✓ (`export *` copies the dependency's exports) |
| Namespace import `import * as ns` | ✓ lowered to a synthetic object literal holding every export |
| Circular dependencies | ✗ errors out (no circular initialization semantics) |
| Live bindings | ✗ namespace objects and imported bindings are snapshots at module-evaluation time |
| Third-party / npm dependencies | ✗ not implemented (relative `.ts` files only) |

> Extension modules (such as `fs`) are importable by bare or `node:`-prefixed
> specifier — `import { readFileSync } from "fs"` / `import path from "path"` —
> and resolve to their runtime entries (named, default and namespace forms).
> Relative modules are bundled at the driver layer.

---

## 5. Unimplemented function features (or missing semantics)

| Feature | Parse | Semantics |
| --- | --- | --- |
| Default parameters `function f(a = 5)` | ✓ | ✓ implemented |
| Rest parameters `function f(...args)` | ✓ | ✓ implemented |
| Spread in call arguments `f(...args)` | ✓ | ✓ implemented |
| Destructuring parameters / bindings | ✓ | ✓ implemented |
| `arguments` object | ✓ (implicit) | ✓ implemented (arrow functions get their own parameters, not the outer function's `arguments`, unlike JS) |
| `this` binding / method call semantics | ✓ | ✓ implemented (`this` is threaded as the first ABI parameter; arrow functions inherit lexically) |
| `async` / `await` / Promise | ✓ | ✓ implemented (synchronous microtask model) |
| `new.target` | ✗ | not implemented |
| Generators / iterators / `yield` | ✗ (`yield` codegen errors) | not implemented |
| Closure `arity` | — | `xt_closure_arity` is always -1, never filled in |
| `fn.call` / `fn.apply` / `fn.bind` | ✓ | ✓ implemented (the bound closure does not track partial-argument `length`) |
| `fn.name` / `fn.length` | ✓ | ✓ implemented (inferred from the declaration / assignment / property key; bound functions use `"bound ..."` and adjusted arity) |
| First-class built-in methods (`typeof arr.map`, `const f = arr.push`, `obj.method?.()`) | ✗ | built-in methods are only reachable through a direct call (`arr.map(...)`); reading them as values yields `undefined` |

---

## 6. Classes and object orientation

| Feature | Status |
| --- | --- |
| `class` declarations / class expressions | ✓ implemented |
| `constructor` | ✓ |
| Instance fields / property declarations | ✓ |
| Methods | ✓ |
| `static` fields / methods | ✓ |
| Inheritance `extends` / `super` | ✓ (single level correct; `super` takes the prototype of `this`'s prototype — see section 8) |
| Prototype chain / method lookup | ✓ |
| `instanceof` | ✓ |
| `new` / instantiation | ✓ |
| `get` / `set` accessors | ✓ implemented |
| Parameter properties `constructor(public x: T)` | ✓ implemented |
| Private fields `#x` | ✓ implemented (stored under a literal `#x` key; no accessibility enforcement) |
| `enum` / `const enum` | ✓ implemented (forward + reverse mapping) |
| `private` / `protected` / `public` / `readonly` modifiers | ✗ no access control (erased) |
| `abstract` / `implements` | ✗ (erased) |
| Parent/child `#x` name collisions | ✗ may alias (same literal key) |

---

## 7. Unimplemented type system (parsed only, not checked)

Type syntax is parsed into the AST and then **erased entirely** during binding /
code generation; no type checking is performed:

- Type annotations, return types, type parameters, type aliases, interfaces, generic constraints, etc.: parse ✓, check ✗.
- Checker diagnostic codes (`TypeMismatch` `NotCallable` `PropertyNotFound` `ArgumentCountMismatch`) are defined but **unused**.
- `as` / `satisfies` / non-null assertion `!`: erased directly, no assertion semantics.
- Generics: no runtime instantiation; type parameters are ignored.
- Optional chaining `?.` type narrowing: none (runtime short-circuit is implemented, but there is no type-level narrowing).

---

## 8. Runtime / semantics differing from the standard (known deviations)

These features **compile and run**, but the result does not fully match ECMAScript:

| Item | Deviation |
| --- | --- |
| String `length` | Counted in UTF-8 bytes, not UTF-16 code units (`"\u00e9".length` reports 1 instead of 2; `"\u{1F600}".length` reports 4 instead of 2; `codePointAt` likewise differs) |
| `Object.getPrototypeOf({})` | Returns `undefined` instead of the `Object.prototype` object |
| Global RegExp `lastIndex` | `test` / `exec` do not advance or honour a caller-set `lastIndex` for `/g` / `/y` regexes |
| String `normalize` / `structuredClone` / `Symbol` | Not implemented (see section 3) |
| First-class built-in methods | `typeof arr.map` is `"undefined"`; a detached built-in method cannot be called and `obj.method?.()` is not bound to `obj` |
| `for...in` | Enumerates own keys of objects / arrays / strings; does not include prototype-chain properties |
| Array out-of-bounds / sparse | Out-of-bounds access returns `undefined`; assigning `arr.length` truncates / extends, but sparse holes are not tracked distinctly |
| Memory management | Bump arena never frees; no GC; long-lived programs grow continuously |
| Function `arity` / argument count | No argument count validation; `fn.length` reports the declared arity but calls are never checked against it |
| `async` / `await` | **Synchronous microtask model**: `await` on an already-settled promise continues synchronously; no real event loop, so timers / I/O cannot be awaited |
| `super` | `super.x` / `super(...)` takes the prototype of `this`'s prototype; single-level inheritance is correct, but depth > 1 may be inaccurate |
| `Error.stack` | Not captured |
| Module live bindings | Namespace imports and imported bindings are snapshots (see section 4) |
| `import.meta` | Parsed but has no value |

> Number formatting, loose equality, `+` / relational `ToPrimitive`, whole-chain
> optional chaining, `finally` on early exit and object-key ordering have all
> been brought in line with Node and are covered by the differential tests, so
> they are no longer listed as deviations.

---

## 9. Unimplemented toolchain / platform / engineering

| Item | Status |
| --- | --- |
| GC (garbage collection) | ✗ deliberately deferred; `xt_alloc` is isolated but not yet replaced with a precise / conservative collector |
| Self-hosting | ✓ the compiler compiles itself: `xbintsc build src/cli/main.ts` produces a working binary, and the emitted IR is stable from generation 1 onward. The runtime is still C |
| Type checker | ✗ only diagnostic codes are defined; no checker |
| Full standard library | partial: Math / JSON / Date / Map / Set / RegExp / `Error` / `BigInt` implemented; Symbol not implemented |
| Multi-file module bundling | partial: relative-path `.ts` bundling, namespace imports and bare-specifier extension module imports implemented; circular dependencies / npm / live bindings not implemented |
| A real async runtime / event loop | ✗ (Promise is a synchronous microtask model) |
| Windows binary artifact verification | adapted at the build layer (`.exe` suffix, link flag branch), verified in CI |
| Precise ECMAScript number / string / comparison semantics | partial, see section 8 |

---

## 10. Quick reference: unimplemented / partially implemented list

```
Unimplemented (statements): namespace/module declarations

Unimplemented (expressions): yield (generators), new.target, import.meta value

Unimplemented (functions): generators, first-class built-in methods
                           first-class built-in methods

Unimplemented (classes/OO): abstract/implements, access control,
                            parent/child #x collision

Unimplemented (standard library): Symbol, String.normalize, structuredClone,
                                  TypeError/RangeError subclasses, AggregateError,
                                  iterator protocol (Symbol.iterator)

Unimplemented (modules): circular dependencies, npm dependencies, live bindings

Unimplemented (type system): type checking, generic instantiation, assertion
                             semantics, optional-chaining narrowing

Unimplemented (runtime): GC, a real async event loop, UTF-16 string length,
                         Object.prototype identity, global-regex lastIndex

Unimplemented (engineering): GC replacement, type checker
```
