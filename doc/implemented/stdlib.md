# Standard library

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/stdlib.md)

Location: `runtime/xt_stdlib.c`, `runtime/xt_stdlib2.c`, `runtime/xt_symbol.c`,
`runtime/xt_builtins.c`, `runtime/xt_constructors.c`, `runtime/xt_io.c`,
`runtime/xt_structured_clone.c`, `runtime/xt_url.c`.

The dispatch layer is `xt_call_method` (array / string methods and function
properties on objects), `xt_math_call` (`Math.*` and constants) and the
`xt_*` entry point of each constructor. Well-known symbols and the iterator
protocol are in [Symbol](#symbol) below; collections and typed arrays have their
own pages: [Collections](./collections.md), [Typed arrays and binary data](./typed-arrays.md).

## Objects

- `Object.keys` / `values` / `entries` / `assign` / `getOwnPropertyNames` / `groupBy`, object spread `{...obj}`.
- `Object.defineProperty` / `defineProperties` / `getOwnPropertyDescriptor(s)` — data and accessor descriptors, one at a time or in bulk; the attribute flags are reported but not enforced on later writes/enumeration.
- `Object.freeze` / `is_frozen` / `from_entries`, `Object.getPrototypeOf`.
- `Object.prototype`: `hasOwnProperty` / `toString` / `valueOf` / `isPrototypeOf` / `propertyIsEnumerable` / `__proto__`; `Object.getPrototypeOf({}) === Object.prototype`.
- First-class built-in constructors (`const A = Array`, `new A(...)`, `A.isArray`, `x instanceof A`) are covered in [Codegen](./codegen.md#llvm-ir-code-generation-codegen).

## Arrays

- `push` `pop` `shift` `unshift` `join` `slice` `splice` `indexOf` `lastIndexOf` `includes` `concat` `reverse` `forEach` `map` `filter` `reduce` `reduceRight` `find` `findIndex` `findLast` `findLastIndex` `some` `every` `sort` `flat` `flatMap` `fill` `copyWithin` `at` `keys` `values` `entries`
- Immutable helpers: `toReversed` / `toSorted` / `toSpliced` / `with`.
- Statics: `Array.isArray` / `of` / `from`.
- Default `sort` is stable and compares string forms, like Node; assigning `arr.length` truncates or extends the array.

## Numbers, BigInt and Math

- Number formatting and coercion match Node: `toFixed` / `toPrecision` / `toExponential`, hex / octal / binary parsing, and the full `+` / relational `ToPrimitive` rules. These are covered by the differential tests.
- `BigInt` literals and values: arbitrary precision arithmetic, bitwise and shift operators, comparisons, `toString(radix)`, `BigInt()`, `BigInt.asIntN` / `BigInt.asUintN`.
- `Math`: the full function set and constants.
- `Math` is also a first-class object: `const cos = Math.cos`, `[1, 2].map(Math.max)` and `const { sqrt, PI } = Math` read members as values (`xt_math_object()` caches one native closure per function, so reads stay cheap).
- `Number` statics and `Number()` conversion; `parseInt` / `parseFloat` / `isNaN` / `isFinite`.

## JSON

- `JSON.parse` with a `reviver`.
- `JSON.stringify` with a function or array `replacer`, `toJSON`, and `space` (number or string, clamped to 10). `undefined` / function / symbol values follow the standard omission rules.
- Object keys are emitted in insertion order, with integer keys ordered numerically; circular references throw.

## Date

- `Date` backed by `gmtime_r`; ISO 8601 parsing through `Date.parse` / `new Date(string)`.

## RegExp

- `RegExp` uses POSIX ERE (`regcomp` / `regexec`) for `test` / `exec`, with capture groups exposed through `exec` / `match` / `replace` / `split` / `search`.
- Matching is byte-oriented and `/g` / `/y` `lastIndex` is not honoured — see [Runtime deviations](../unimplemented/deviations.md).

## Symbol

- `Symbol(description)` primitives (`XT_OBJECT_KIND_SYMBOL`), the 13 well-known symbols (`Symbol.iterator`, `Symbol.asyncIterator`, `Symbol.match`, ...), `Symbol.for` / `Symbol.keyFor` global registry, `symbol.description` / `toString()` / `valueOf()`.
- Symbols are valid property keys (`Object.getOwnPropertySymbols`, symbol keyed `get`/`set`/`in`/`delete`), are skipped by `Object.keys` / `values` / `entries` / `for...in` / `JSON.stringify`, and print as `Symbol(desc)`.

## URL

- `URL` is implemented in `runtime/xt_url_object.c`: `new URL(input)` and
  `new URL(input, base)` (the base may be another `URL`), resolving relative
  references against a hierarchical `scheme://authority/path` base.
- A reference with **no scheme is a filesystem path**: `new URL("./model.glb")`
  resolves against the working directory and yields a `file:` URL (an explicit
  base is still honoured, so `new URL("./x", import.meta.url)` works too).
- Read-only `href`, `protocol`, `origin`, `host`, `hostname`, `port`,
  `pathname`, `search`, `hash`, `searchParams` and `toString()` / `toJSON()`;
  absolute inputs are kept as written, except that the Windows spellings of a
  `file:` URL (`file://C:/dir` and `file://C:\dir`) are canonicalized to
  `file:///C:/dir`, matching Node.
- Not a full WHATWG parser: path percent-encoding, IDNA, setters and live
  `searchParams` synchronisation are not implemented — see
  [Runtime deviations](../unimplemented/deviations.md).

## URLSearchParams

- `URLSearchParams` is implemented in `runtime/xt_url.c`.

## Errors

- The `Error` family: `Error`, `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError`, `EvalError`, `URIError`, `AggregateError` — `new`, `instanceof`, `extends`, `String(err)` / `err.toString()` and `.stack`.
- `Promise.any` rejects with an `AggregateError`.
- `.stack` contains function-level frames (`at name (file:line)`), captured from the runtime shadow call stack; the exact limitations are in [Runtime deviations](../unimplemented/deviations.md).

## console

- `xt_print` / `xt_println` / `xt_console_log` / `info` / `warn` / `error`; `info` / `log` go to stdout, `warn` / `error` to stderr.
- Node-style inspection: arrays `[ a, b ]`, objects `{ k: v }`, UTF-8-aware, with circular references marked.
- `dir` / `trace` / `assert` / `count` / `countReset` / `group` / `groupEnd` / `table` / `time` / `timeEnd` / `timeLog`.

## Global functions

- `parseInt` `parseFloat` `isNaN` `isFinite` `Number` `String` `Boolean` `encodeURI` `decodeURI` `encodeURIComponent` `decodeURIComponent`.
- `btoa` / `atob` base64 encoding and decoding (core globals; not Node-extension-only).
- `structuredClone`: arrays, objects, `Map`, `Set`, `Date`, `RegExp` and typed arrays, preserving cycles and shared references; functions, symbols, promises and generators are rejected.
- Timers, `queueMicrotask` and `performance.now` are described in [Async, promises and generators](./async.md); `fetch` in [fetch](./fetch.md).
