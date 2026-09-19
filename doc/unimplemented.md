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

> Recently completed (this batch): `class` declarations / class expressions, `new` / `this`, inheritance `extends` / `super`, `instanceof`, methods / static members / instance fields, `async` / `await` + `Promise` (`then/catch/finally`, `resolve/reject/all/allSettled/race`), multi-file `import` / `export` bundling, `Map` / `Set` / `Date` / `RegExp` / `JSON`, and a large set of array / string / number / object / console extension methods. See the [implemented document](./implemented.md).

> Completed earlier: `switch`, `try/catch/finally`, object spread, `delete`, `in`, array / string methods, `Math`, `Object.keys/values/entries/assign`, global functions, `console.error/warn/info`, `arguments`, default / rest parameters, optional chaining short-circuit, `for...in` object key enumeration.

---

## 1. Unimplemented at the statement level

### 1.1 Parsed but codegen reports `UnsupportedFeature`

| Syntax | Status | Notes |
| --- | --- | --- |
| `enum` declarations | parse ✓, codegen ✗ | `enum E { A, B }` → "does not yet support this statement (enum declaration)" |
| `namespace` / `module` declarations | parse ✓, codegen ✗ | → "does not yet support this statement (module declaration)" |
| `label: statement` | parse ✓, codegen ✗ | no labeled jump semantics |

### 1.2 Not supported by the parser (immediate syntax error)

| Syntax | Status | Notes |
| --- | --- | --- |
| Labeled statements `label: statement` (some paths) | partially parsed | `parseStatement` may still report "Unexpected token ':'" for some `label:` forms |

> `class` / `new` / `this` / `import` / `export` are now implemented; see below.

---

## 2. Unimplemented at the expression level

### 2.1 Parsed but codegen reports `UnsupportedFeature`

| Syntax | Status | Notes |
| --- | --- | --- |
| Tagged templates `` f`...` `` | parse ✓, codegen ✗ | → "does not yet support this expression (tagged template)" |
| `yield` expressions (generators) | parse ✓, codegen ✗ | → "does not yet support this expression (yield expression)" |
| Array destructuring / object destructuring | parse ✓, codegen ✗ | neither destructuring bindings nor destructuring assignment are implemented |

### 2.2 Not supported by the parser

| Syntax | Status | Notes |
| --- | --- | --- |
| Regex literal as an expression | not parsed | the scanner can scan `/re/`, but `parsePrimaryExpression` has no `RegularExpressionLiteral` branch and reports "Unexpected token '/…/'"; use `new RegExp(...)` instead |
| `Promise.any` / type keywords as property names (e.g. `.any`, `.get` in some cases) | partially unparsed | contextual keywords as member names occasionally report "Expected identifier" |
| Spread in call arguments `f(...args)` / `Math.max(...xs)` | not parsed | `SpreadElement` in argument position reports unsupported |

### 2.3 Unimplemented operators (codegen errors)

| Operator | Status | Notes |
| --- | --- | --- |
| `typeof` / `void` | implemented | — |
| `instanceof` | implemented | mapped to `xt_instance_of` |

> Value-level `typeof` / `void` are implemented (see the implemented document). `in` and `delete` are implemented.

---

## 3. Unimplemented standard library

### 3.1 Implemented (summary)

- Arrays: `push` `pop` `shift` `unshift` `join` `slice` `indexOf` `includes` `concat` `reverse` `forEach` `map` `filter` `reduce`
- Strings: `charAt` `charCodeAt` `indexOf` `includes` `slice` `substring` `substr` `split` `toUpperCase` `toLowerCase` `trim` `replace` `repeat` `startsWith` `endsWith` `concat`
- `Math`: `abs` `floor` `ceil` `round` `trunc` `sqrt` `cbrt` `pow` `exp` `log` `log2` `log10` `sin` `cos` `tan` `asin` `acos` `atan` `atan2` `hypot` `sign` `random` `min` `max`, constants `PI` `E` `LN2` `LN10` `LOG2E` `LOG10E` `SQRT2` `SQRT1_2`
- `Object.keys` / `values` / `entries` / `assign`, object spread `{...obj}`
- Global functions: `parseInt` `parseFloat` `isNaN` `isFinite` `Number` `String` `Boolean`
- `console.log` / `info` / `warn` / `error`

### 3.2 Completed (this batch)

- `JSON.parse` / `JSON.stringify`
- `Date` (construction, `getTime`, `getFullYear`/`getUTCFullYear` etc., `toISOString`/`toJSON`), `RegExp` (`new RegExp`, `test`, `exec`, a POSIX ERE subset)
- `Map` (`set/get/has/delete/clear/size`), `Set` (`add/has/delete/clear/size`)
- `Promise` (`resolve/reject/all/allSettled/race`, instance `then/catch/finally`)
- `Array` statics (`isArray/of/from`), `Object` statics (`keys/values/entries/assign/freeze/isFrozen/fromEntries/getPrototypeOf/setPrototypeOf/hasOwn/is/create`), `Number` statics (`isInteger/isSafeInteger/isFinite/isNaN/parseInt/parseFloat` and constants), `String` statics (`fromCharCode/fromCodePoint/raw`)
- Array instance extensions: `at/find/findIndex/findLast/findLastIndex/some/every/sort/flat/flatMap/lastIndexOf/fill/copyWithin/reduceRight/toString/keys/values/entries`
- String instance extensions: `at/padStart/padEnd/trimStart/trimEnd/replaceAll/localeCompare/codePointAt/valueOf`
- Number instances: `toFixed/toPrecision/toExponential/toString(radix)/valueOf`
- `Math` extensions: `log1p/expm1/sinh/cosh/tanh/asinh/acosh/atanh/fround/imul/clz32`
- `console` extensions: `dir/trace/assert/count/countReset/group/groupEnd/table/time/timeEnd/timeLog`

### 3.3 Still unimplemented

| Category | Status |
| --- | --- |
| `Symbol` constructor and symbol primitives | ✗ not implemented |
| `BigInt` arbitrary precision | ✗ (literals degrade to double) |
| `Error` constructor / `message` / `stack` | ✗ not implemented (any value can be thrown) |
| Timers / I/O / process and other host APIs | only via extensions (e.g. Node `fs`) |
| Iterator protocol / `Symbol.iterator` / custom `for...of` iterables | ✗ not implemented |
| Generators / async iteration | ✗ not implemented |

---

## 4. Module system (partially implemented)

| Feature | Status |
| --- | --- |
| Multi-file / module resolution and linking | ✓ driver-layer AST bundling (`src/driver/modules.ts`): parse each module, rename top-level symbols by module prefix, rewrite references, merge into one file and rebind |
| Named import/export | ✓ `import { a, b as c }` / `export { a as b }` / `export const/let/var/function/class` |
| Default import/export | ✓ `export default` / `import d from` |
| Re-export `export { x } from` / `export * from` | ✓ (`export *` is an approximate copy) |
| Namespace import `import * as ns` | ✗ not implemented for relative modules (extension modules such as `path` support it: `import * as path from "path"`) |
| Circular dependencies | ✗ errors out (no circular initialization semantics) |
| Third-party / npm dependencies | ✗ not implemented (relative `.ts` files only) |

> Extension modules (such as `fs`) are importable by bare or `node:`-prefixed specifier — `import { readFileSync } from "fs"` / `import path from "path"` — and resolve to their runtime entries (named, default and namespace forms). Relative modules are still bundled at the driver layer.

---

## 5. Unimplemented function features (or missing semantics)

| Feature | Parse | Semantics |
| --- | --- | --- |
| Default parameters `function f(a = 5)` | ✓ | ✓ implemented |
| Rest parameters `function f(...args)` | ✓ | ✓ implemented |
| `arguments` object | ✓ (implicit) | ✓ implemented (arrow functions get their own parameters, not the outer function's `arguments`, unlike JS) |
| `this` binding / method call semantics | ✓ | ✓ implemented (`this` is threaded as the first ABI parameter; arrow functions inherit lexically) |
| `async` / `await` / Promise | ✓ | ✓ implemented (synchronous microtask model) |
| `new.target` | ✗ | not implemented |
| Generators / iterators / `yield` | ✗ (`yield` codegen errors) | not implemented |
| Closure `arity` / function properties | — | `xt_closure_arity` is always -1, never filled in |
| Function object properties (`fn.name` / `fn.length` / `fn.call` / `fn.apply` / `bind`) | ✗ | not implemented |
| Spread in call arguments `f(...args)` | ✗ | not implemented |
| Destructuring parameters / bindings | ✗ | not implemented |

---

## 6. Classes and object orientation (partially implemented)

| Feature | Status |
| --- | --- |
| `class` declarations / class expressions | ✓ implemented (prototype object + constructor closure, stored in an LLVM global) |
| `constructor` | ✓ |
| Instance fields / property declarations | ✓ (including `this` initializers, executed before the constructor body) |
| Methods | ✓ |
| `static` fields / methods | ✓ (stored in the constructor function's property bag) |
| Inheritance `extends` / `super` | ✓ (single level correct; `super` takes the prototype of `this`'s prototype — see the theoretical flaw in section 8) |
| Prototype chain / method lookup | ✓ |
| `instanceof` | ✓ |
| `new` / instantiation | ✓ |
| `get` / `set` accessors | ✗ parsed only, no accessor semantics |
| Parameter properties `constructor(public x: T)` | ✗ does not auto-assign `this.x` |
| `private` / `protected` / `public` / `readonly` modifiers | ✗ parsed only, no access control |
| `abstract` / `implements` | ✗ |
| Private fields `#x` | ✗ |
| `enum` | ✗ |

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
| `finally` and early exit | `return` / `break` / `continue` leaving a `try` region **does not execute `finally`**; `finally` runs on normal completion, after `catch` completes, and on propagation of an uncaught exception. The runtime still pops the `try` frame correctly, so it does not crash |
| Exception objects | Any value can be thrown / caught (string, number, object), but there is no `Error` constructor, `message` / `stack`, or error subclasses |
| `for...in` | Enumerates keys of objects / arrays / strings (arrays and strings yield string indices), but does not include prototype chain properties; behavior after `delete` is broadly consistent with JS |
| String `length` | Counted by UTF-8 bytes / code points at runtime, not by JS's UTF-16 code units (emoji and non-BMP characters report a smaller length) |
| BigInt | Literals are converted to double via `Number()`, losing arbitrary precision |
| Number-to-string | Only common cases are covered (integers, shortest round-trip); boundary formatting (scientific notation details, etc.) differs from JS |
| Loose equality `==` | Only a subset is implemented (number/string/bool/null/undefined); objects compare by reference, no ToPrimitive |
| `+` addition | The ToPrimitive path for number + object / array is incomplete |
| Object spread `{...obj}` | Only copies the object's own enumerable properties; index copying for array / string spread is limited |
| Optional chaining `?.` | Nullish short-circuit is node-by-node: `a?.b`, `a?.[b]`, `a?.b()`, `a?.[b]()`, `a?.()` all short-circuit correctly; but a non-optional member chained after the optional part (e.g. `a?.b.c()`) does not short-circuit as a whole — `a?.b.c` first yields `undefined`, then `.c` is taken on it, and the final call throws |
| Array out-of-bounds / sparse | Out-of-bounds access returns `undefined` and is generally usable, but length / sparse semantics differ from JS |
| Memory management | Bump arena never frees; no GC; long-lived programs grow continuously |
| Function `arity` / argument count | No argument count validation |
| `async` / `await` | **Synchronous microtask model**: `await` on an already-settled promise continues synchronously, and pending promises are driven by the runtime microtask queue at `await` and program exit; there is no real event loop, so timers / I/O cannot be awaited |
| `super` | `super.x` / `super(...)` takes the prototype of `this`'s prototype; single-level inheritance is correct, but depth > 1 may be inaccurate |
| Classes | No `get`/`set` accessor semantics, no access control, no automatic parameter property assignment |
| `import` / `export` | Driver-layer AST bundling, top-level symbols renamed by module prefix; extension modules importable by bare/`node:` specifier; namespace imports `import * as` of relative modules are unimplemented, circular dependencies error out, `export *` is approximate |

---

## 9. Unimplemented toolchain / platform / engineering

| Item | Status |
| --- | --- |
| GC (garbage collection) | ✗ deliberately deferred; `xt_alloc` is isolated but not yet replaced with a precise / conservative collector |
| Self-hosting | ✗ roadmap planned (see [DESIGN.md](./DESIGN.md) / [README](../README.md)), not yet implemented: the runtime is still C, and the compiler does not compile itself with xbintsc |
| Type checker | ✗ only diagnostic codes are defined; no checker |
| Full standard library (Math / JSON / Date / collections, etc.) | partial: Math / JSON / Date / Map / Set / RegExp implemented; Symbol / BigInt / Error not implemented |
| Multi-file module bundling | partial: relative-path `.ts` import bundling implemented, plus bare-specifier extension module imports; namespace imports of relative modules / circular dependencies / npm not implemented |
| A real async runtime / event loop | ✗ (Promise is a synchronous microtask model) |
| Windows binary artifact verification | adapted at the build layer (`.exe` suffix, link flag branch), but needs CI verification (`.github/workflows` is configured) |
| Precise ECMAScript number / string / comparison semantics | ✗ see section 8 |

---

## 10. Quick reference: unimplemented / partially implemented list

```
Unimplemented (statements): enum, namespace/module, labeled statements label:

Unimplemented (expressions): regex literals, tagged templates, yield, array/object destructuring,
                             spread in call arguments f(...args)

Unimplemented (functions): generators, fn.name/length/call/apply/bind, new.target

Unimplemented (classes/OO): get/set accessors, access control, parameter properties, private fields #x, enum

Unimplemented (standard library): Symbol, BigInt arbitrary precision, Error constructor, iterator protocol

Unimplemented (modules): namespace imports import * as of relative modules, circular dependencies, npm dependencies

Unimplemented (type system): type checking, generic instantiation, assertion semantics, optional-chaining narrowing

Unimplemented (runtime): GC, Error constructor, a real async event loop, finally on early exit,
                        UTF-16 length, BigInt precision, full ToPrimitive path

Unimplemented (engineering): self-hosting, GC replacement, type checker
```
