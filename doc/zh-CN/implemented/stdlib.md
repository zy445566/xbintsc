# 标准库

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/stdlib.md)

实现位置：`runtime/xt_stdlib.c`、`runtime/xt_stdlib2.c`、`runtime/xt_symbol.c`、
`runtime/xt_builtins.c`、`runtime/xt_constructors.c`、`runtime/xt_io.c`、
`runtime/xt_structured_clone.c`、`runtime/xt_url.c`。

分发层为 `xt_call_method`（数组 / 字符串方法与对象上的函数属性）、`xt_math_call`
（`Math.*` 与常量）以及各构造器的 `xt_*` 入口。集合与类型化数组有独立页面：
[集合](./collections.md)、[类型化数组与二进制数据](./typed-arrays.md)。

## 对象

- `Object.keys` / `values` / `entries` / `assign` / `getOwnPropertyNames` / `groupBy`，对象展开 `{...obj}`。
- `Object.defineProperty` / `defineProperties` / `getOwnPropertyDescriptor(s)` —— 数据属性与访问器属性，可单个也可批量定义；属性标志会被报告，但后续写入/枚举尚未强制执行。
- `Object.freeze` / `is_frozen` / `from_entries`、`Object.getPrototypeOf`。
- `Object.prototype`：`hasOwnProperty` / `toString` / `valueOf` / `isPrototypeOf` / `propertyIsEnumerable` / `__proto__`；`Object.getPrototypeOf({}) === Object.prototype`。
- 一等公民内建构造器（`const A = Array`、`new A(...)`、`A.isArray`、`x instanceof A`）见[代码生成](./codegen.md)。

## 数组

- `push` `pop` `shift` `unshift` `join` `slice` `splice` `indexOf` `lastIndexOf` `includes` `concat` `reverse` `forEach` `map` `filter` `reduce` `reduceRight` `find` `findIndex` `findLast` `findLastIndex` `some` `every` `sort` `flat` `flatMap` `fill` `copyWithin` `at` `keys` `values` `entries`
- 不可变方法：`toReversed` / `toSorted` / `toSpliced` / `with`。
- 静态：`Array.isArray` / `of` / `from`。
- 默认 `sort` 稳定，并按字符串形式比较（与 Node 一致）；对 `arr.length` 赋值会截断 / 扩展数组。

## 数字、BigInt 与 Math

- 数字格式化与强制转换与 Node 一致：`toFixed` / `toPrecision` / `toExponential`、十六 / 八 / 二进制解析，以及完整的 `+` / 关系运算 `ToPrimitive` 规则。这些由差分测试覆盖。
- `BigInt` 字面量与值：任意精度算术、位运算与移位、比较、`toString(radix)`、`BigInt()`、`BigInt.asIntN` / `BigInt.asUintN`。
- `Math`：完整函数与常量。
- `Math` 同时是一等对象：`const cos = Math.cos`、`[1, 2].map(Math.max)`、`const { sqrt, PI } = Math` 都能按值读取成员（`xt_math_object()` 为每个函数缓存一个原生闭包，读取开销仍然很低）。
- `Number` 静态与 `Number()` 转换；`parseInt` / `parseFloat` / `isNaN` / `isFinite`。

## JSON

- `JSON.parse` 支持 `reviver`。
- `JSON.stringify` 支持函数或数组 `replacer`、`toJSON`，以及 `space`（数字或字符串，最多 10 个单位）。`undefined` / 函数 / symbol 值遵循标准省略规则。
- 对象键按插入序输出，整数键按数值排序；循环引用抛错。

## Date

- `Date` 基于 `gmtime_r`；`Date.parse` / `new Date(string)` 支持 ISO 8601 解析。

## RegExp

- `RegExp` 使用 POSIX ERE（`regcomp` / `regexec`）实现 `test` / `exec`；捕获组可经 `exec` / `match` / `replace` / `split` / `search` 使用。
- 匹配按字节进行，且 `/g` / `/y` 的 `lastIndex` 不被读取 —— 见[运行时偏差](../unimplemented/deviations.md)。

## Symbol

- `Symbol(description)` 原始值（`XT_OBJECT_KIND_SYMBOL`）、13 个著名符号（`Symbol.iterator`、`Symbol.asyncIterator`、`Symbol.match` 等）、`Symbol.for` / `Symbol.keyFor` 全局注册表、`symbol.description` / `toString()` / `valueOf()`。
- symbol 可作为属性键（`Object.getOwnPropertySymbols`，symbol 键的 `get`/`set`/`in`/`delete`），会被 `Object.keys` / `values` / `entries` / `for...in` / `JSON.stringify` 跳过，打印为 `Symbol(desc)`。

## URLSearchParams

- `URLSearchParams` 实现在 `runtime/xt_url.c`。

## URL

- `URL` 实现在 `runtime/xt_url_object.c`：支持 `new URL(input)` 与
  `new URL(input, base)`（base 可以是另一个 `URL`），可基于
  `scheme://authority/path` 形式的 base 解析相对引用。
- 只读属性 `href`、`protocol`、`origin`、`host`、`hostname`、`port`、
  `pathname`、`search`、`hash` 与 `toString()`；绝对地址按原样保留。
- 不是完整的 WHATWG 解析器：路径百分号编码、IDNA、setter 与 `searchParams`
  未实现 —— 见 [运行时差异](../unimplemented/deviations.md)。

## 错误

- `Error` 家族：`Error`、`TypeError`、`RangeError`、`SyntaxError`、`ReferenceError`、`EvalError`、`URIError`、`AggregateError` —— 支持 `new`、`instanceof`、`extends`、`String(err)` / `err.toString()` 与 `.stack`。
- `Promise.any` 以 `AggregateError` 作为 rejection。
- `.stack` 仅含函数级帧（`at 名称 (文件:行)`），取自运行时影子调用栈；具体限制见[运行时偏差](../unimplemented/deviations.md)。

## console

- `xt_print` / `xt_println` / `xt_console_log` / `info` / `warn` / `error`；`info` / `log` 到 stdout，`warn` / `error` 到 stderr。
- Node 风格 inspect：数组 `[ a, b ]`、对象 `{ k: v }`，感知 UTF-8，循环引用会被标记。
- `dir` / `trace` / `assert` / `count` / `countReset` / `group` / `groupEnd` / `table` / `time` / `timeEnd` / `timeLog`。

## 全局函数

- `parseInt` `parseFloat` `isNaN` `isFinite` `Number` `String` `Boolean` `encodeURI` `decodeURI` `encodeURIComponent` `decodeURIComponent`。
- `btoa` / `atob` base64 编解码（核心全局函数，并非 Node 扩展专属）。
- `structuredClone`：数组、对象、`Map`、`Set`、`Date`、`RegExp` 与类型化数组，保留循环引用与共享引用；函数、symbol、Promise、生成器会被拒绝。
- 定时器、`queueMicrotask` 与 `performance.now` 见[异步、Promise 与生成器](./async.md)；`fetch` 见 [fetch](./fetch.md)。
