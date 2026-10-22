# Standard library

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/stdlib.md)

## Implemented (summary)

- Arrays: `push` `pop` `shift` `unshift` `join` `slice` `splice` `indexOf` `lastIndexOf` `includes` `concat` `reverse` `forEach` `map` `filter` `reduce` `reduceRight` `find` `findIndex` `findLast` `findLastIndex` `some` `every` `sort` `flat` `flatMap` `fill` `copyWithin` `at` `keys` `values` `entries` and the immutable `toReversed` / `toSorted` / `toSpliced` / `with`
- Strings: `charAt` `charCodeAt` `codePointAt` `indexOf` `lastIndexOf` `includes` `startsWith` `endsWith` `slice` `substring` `substr` `split` `match` `replace` `replaceAll` `search` `toUpperCase` `toLowerCase` `trim` `trimStart` `trimEnd` `padStart` `padEnd` `repeat` `concat` `at` `localeCompare` `valueOf`
- `Math`: full set of functions and constants
- `Object.keys` / `values` / `entries` / `assign` / `getOwnPropertyNames` / `groupBy`, object spread `{...obj}`
- Global functions: `parseInt` `parseFloat` `isNaN` `isFinite` `Number` `String` `Boolean` `encodeURI` `decodeURI` `encodeURIComponent` `decodeURIComponent`
- `console.log` / `info` / `warn` / `error` / `dir` / `trace` / `assert` / `count` / `group` / `table` / `time`

## Completed

- `JSON.parse` / `JSON.stringify`
- `Date`, `RegExp` (POSIX ERE subset with capture groups), `Map`, `Set`, `Promise`, `BigInt`
- `Array` statics (`isArray/of/from`), `Object` statics, `Number` statics, `String` statics (`fromCharCode` / `fromCodePoint` / `raw`)
- Built-in constructors as first-class values (`const A = Array`, `new A(...)`, `A.isArray`, `x instanceof A`) and `Object.prototype` (`hasOwnProperty`, `toString`, `valueOf`, `isPrototypeOf`, `propertyIsEnumerable`, `__proto__`), with `Object.getPrototypeOf({}) === Object.prototype`
- `Error` family (`Error`, `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError`, `EvalError`, `URIError`, `AggregateError`): `new`, `instanceof`, `extends`, `String(err)` / `err.toString()` and `.stack`

## Still unimplemented

| Category | Status |
| --- | --- |
| `Symbol` constructor and symbol primitives | ✓ `Symbol(description)`, well-known symbols, `Symbol.for` / `Symbol.keyFor`, symbol property keys and `Object.getOwnPropertySymbols` |
| `String.prototype.normalize` | ✗ not implemented |
| `structuredClone` | ✓ arrays, objects, Map, Set, Date, RegExp and typed arrays (cycles and shared references preserved); functions, symbols, promises and generators are rejected |
| `JSON.stringify` options | ✓ `space` (number/string, clamped to 10), a function/array `replacer`, and `toJSON` are honored |
| Built-in error subclasses (`TypeError`, `RangeError`, ...) | ✓ first-class constructors and prototypes; `instanceof Error` holds for the whole family |
| `AggregateError` constructor | ✓ `new AggregateError(errors, message)`; `Promise.any` now rejects with one |
| Iterator protocol / `Symbol.iterator` / custom `for...of` iterables | ✓ arrays, strings, `Map`, `Set`, generators and any object exposing `[Symbol.iterator]()` are iterable in `for...of` / spread |
| Generators (`function*`, `yield`, `yield*`, `next`/`throw`/`return`) | ✓ implemented with stackful coroutines; `for...of`, spread and delegation supported. `return()` completes without running `finally` |
| Timers and microtasks | ✓ `setTimeout` / `clearTimeout` / `setInterval` / `clearInterval` / `setImmediate` / `clearImmediate` / `queueMicrotask` and `performance.now` (numeric ids, no `Timeout` / `Immediate` object). A promise settled from a timer can be awaited: the async body suspends on the pending promise and resumes when the timer fires |
| I/O / process and other host APIs | only via extensions (e.g. Node `fs`) |

## Typed arrays

Implemented: `Uint8Array`, `Int8Array`, `Uint8ClampedArray`, `Uint16Array`,
`Int16Array`, `Uint32Array`, `Int32Array`, `Float32Array`, `Float64Array` with
`new X(n)` / `new X(arrayLike)`, `X.from` / `X.of`, `BYTES_PER_ELEMENT`, coerced
element writes, and `fill` / `set` / `slice` / `subarray` / `join` / `toString` /
`indexOf` / `lastIndexOf` / `includes` / `forEach` / `map` / `filter` / `every` /
`some` / `find` / `findIndex` / `reduce` / `reverse` / `sort` / `copyWithin` /
`at` / `keys` / `values` / `entries`. Known divergences:

| Feature | Status |
| --- | --- |
| `subarray` | ✓ returns a view over the same buffer; `slice` copies |
| `keys` / `values` / `entries` | ~ return arrays, not iterator objects |
| `ArrayBuffer` / `DataView` / `.buffer` / `.byteOffset` semantics | ✓ `ArrayBuffer` (`slice`, `isView`), `DataView` (big-endian by default, `littleEndian` option), typed arrays backed by a buffer and exposing `.buffer` / `.byteOffset`; `new X(buffer, offset, length)` creates views |
| `BigInt64Array` / `BigUint64Array` | ✗ not implemented |
| `instanceof` built-in constructors | ✓ `instanceof Array` / `Object` / `Map` / `Set` / `Date` / `RegExp` / `Promise` / `Error` family / typed arrays are recognised by name **and through a first-class constructor value** (`const A = Array; x instanceof A`); constructors can be stored, passed, aliased, called and used with `new` |
| Typed-array views over a `Buffer` / `ArrayBuffer` | ✓ typed arrays and `DataView` share the `ArrayBuffer` bytes; `Buffer`-backed typed-array views are still not implemented |
