# 标准库

> 属于[未实现语法与功能](../unimplemented.md) · [English](../../unimplemented/stdlib.md)

## 已实现（概要）

- 数组：`push` `pop` `shift` `unshift` `join` `slice` `splice` `indexOf` `lastIndexOf` `includes` `concat` `reverse` `forEach` `map` `filter` `reduce` `reduceRight` `find` `findIndex` `findLast` `findLastIndex` `some` `every` `sort` `flat` `flatMap` `fill` `copyWithin` `at` `keys` `values` `entries`，以及不可变的 `toReversed` / `toSorted` / `toSpliced` / `with`
- 字符串：`charAt` `charCodeAt` `codePointAt` `indexOf` `lastIndexOf` `includes` `startsWith` `endsWith` `slice` `substring` `substr` `split` `match` `replace` `replaceAll` `search` `toUpperCase` `toLowerCase` `trim` `trimStart` `trimEnd` `padStart` `padEnd` `repeat` `concat` `at` `localeCompare` `valueOf`
- `Math`：完整函数与常量
- `Object.keys` / `values` / `entries` / `assign` / `getOwnPropertyNames` / `groupBy`，对象展开 `{...obj}`
- 全局函数：`parseInt` `parseFloat` `isNaN` `isFinite` `Number` `String` `Boolean` `encodeURI` `decodeURI` `encodeURIComponent` `decodeURIComponent`
- `console.log` / `info` / `warn` / `error` / `dir` / `trace` / `assert` / `count` / `group` / `table` / `time`

## 已补齐

- `JSON.parse` / `JSON.stringify`
- `Date`、`RegExp`（POSIX ERE 子集，支持捕获组）、`Map`、`Set`、`Promise`、`BigInt`
- `Array` 静态（`isArray/of/from`）、`Object` 静态、`Number` 静态、`String` 静态（`fromCharCode` / `fromCodePoint` / `raw`）
- 一等公民内建构造器（`const A = Array`、`new A(...)`、`A.isArray`、`x instanceof A`）与 `Object.prototype`（`hasOwnProperty`、`toString`、`valueOf`、`isPrototypeOf`、`propertyIsEnumerable`、`__proto__`），且 `Object.getPrototypeOf({}) === Object.prototype`
- `Error` 家族（`Error`、`TypeError`、`RangeError`、`SyntaxError`、`ReferenceError`、`EvalError`、`URIError`、`AggregateError`）：支持 `new`、`instanceof`、`extends`、`String(err)` / `err.toString()` 与 `.stack`

## 仍未实现

| 类别 | 现状 |
| --- | --- |
| `Symbol` 构造器与 symbol 原始值 | ✓ `Symbol(description)`、著名符号、`Symbol.for` / `Symbol.keyFor`、symbol 属性键与 `Object.getOwnPropertySymbols` |
| `String.prototype.normalize` | ✗ 未实现 |
| `structuredClone` | ✓ 数组、对象、Map、Set、Date、RegExp 与类型化数组（保留循环引用与共享引用）；函数、symbol、Promise、生成器会被拒绝 |
| `JSON.stringify` 选项 | ✓ 支持 `space`（数字或字符串，最多 10 个单位）、函数/数组 `replacer` 与 `toJSON` |
| 内置错误子类（`TypeError`、`RangeError` 等） | ✓ 一等公民构造器与原型；整个家族 `instanceof Error` 成立 |
| `AggregateError` 构造器 | ✓ `new AggregateError(errors, message)`；`Promise.any` 现以其作为 rejection |
| 迭代器协议 / `Symbol.iterator` / `for...of` 自定义可迭代 | ✓ 数组、字符串、`Map`、`Set`、生成器以及任何暴露 `[Symbol.iterator]()` 的对象均可在 `for...of` / 展开中使用 |
| 生成器（`function*`、`yield`、`yield*`、`next`/`throw`/`return`） | ✓ 基于有栈协程实现；支持 `for...of`、展开与委托；`async function*`、`for await...of` 与 `Symbol.asyncIterator` 也已支持 |
| 定时器与微任务 | ✓ 核心事件循环提供 `setTimeout` / `clearTimeout` / `setInterval` / `clearInterval` / `setImmediate` / `clearImmediate` / `queueMicrotask` 与 `performance.now`（返回数字 id，无 `Timeout` / `Immediate` 对象）。定时器 settle 的 promise 可以被 await：async 函数体在 pending promise 上挂起，定时器触发时恢复 |
| I/O / 进程等宿主 API | 仅通过扩展（如 Node `fs`）提供 |

## 类型化数组

已实现：`Uint8Array`、`Int8Array`、`Uint8ClampedArray`、`Uint16Array`、
`Int16Array`、`Uint32Array`、`Int32Array`、`Float32Array`、`Float64Array`，支持
`new X(n)` / `new X(数组样)`、`X.from` / `X.of`、`BYTES_PER_ELEMENT`、按类型强制的
元素写入，以及 `fill` / `set` / `slice` / `subarray` / `join` / `toString` /
`indexOf` / `lastIndexOf` / `includes` / `forEach` / `map` / `filter` / `every` /
`some` / `find` / `findIndex` / `reduce` / `reverse` / `sort` / `copyWithin` /
`at` / `keys` / `values` / `entries`。已知差异：

| 功能 | 现状 |
| --- | --- |
| `subarray` | ✓ 返回共享同一缓冲区的视图；`slice` 复制 |
| `keys` / `values` / `entries` | ~ 返回数组，而非迭代器对象 |
| `ArrayBuffer` / `DataView` / `.buffer` / `.byteOffset` 语义 | ✓ `ArrayBuffer`（`slice`、`isView`）、`DataView`（默认大端，可选 `littleEndian`）、类型化数组由缓冲区支撑并暴露 `.buffer` / `.byteOffset`；`new X(buffer, offset, length)` 创建视图 |
| `BigInt64Array` / `BigUint64Array` | ✗ 未实现 |
| `instanceof` 内建构造器 | ✓ `instanceof Array` / `Object` / `Map` / `Set` / `Date` / `RegExp` / `Promise` / `Error` 家族 / 类型化数组都能按名字识别，**也能通过一等公民构造器值判断**（`const A = Array; x instanceof A`）；构造器可保存、传参、取别名、调用与 `new` |
| 基于 `Buffer` / `ArrayBuffer` 的类型化数组视图 | ✓ 类型化数组与 `DataView` 共享 `ArrayBuffer` 字节；基于 `Buffer` 的视图仍未实现 |
