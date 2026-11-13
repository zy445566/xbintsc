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
- 函数值的 `.prototype` 在首次读取时创建（带 `constructor` 反向引用），并由同一
  声明的所有闭包共享，因此 ES5 写法 `function Foo() {}; Foo.prototype.bar = ...;
  new Foo().bar()` 可用。函数**值**本身仍是每次引用新建（`Foo === Foo` 为 false）。
- `Object.getOwnPropertyNames` / `Object.keys` 对 `Map` / `Set` / `Date` /
  `RegExp` / `Promise` / `Symbol` 返回空的自有字符串键，而不会越界读取。

## `globalThis` 与 `Intl`

- `globalThis.x` 与 `global.x` 按裸全局名读取（`globalThis.fetch`、`globalThis.Deno`）；
  全局对象本身还不是值，所以 `const g = globalThis` 和 `globalThis.x = 1` 仍不支持。
- `Intl.Segmenter` 支持 `granularity: "grapheme" | "word"`；`segment(text)` 返回
  `{ segment, index, isWordLike }` 的**数组**。这是 UAX #29 的近似实现（未接入 ICU）：
  字素按码点 + 组合符 / 变体选择符 / emoji 修饰符 / ZWJ 序列 / 区域指示符成对切分；
  词按空白、单词字符、标点分段。`resolvedOptions()` 返回 `{ locale: "en", granularity }`。
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

- `JSON.parse` 支持 `reviver`；畸形输入（错误数字/字符串、尾随内容、缺少分隔符）会抛出可捕获的 `SyntaxError`，而不是返回 `undefined`。
- `JSON.stringify` 支持函数或数组 `replacer`、`toJSON`，以及 `space`（数字或字符串，最多 10 个单位）。`undefined` / 函数 / symbol 值遵循标准省略规则。
- 对象键按插入序输出，整数键按数值排序；循环引用抛错。

## Date

- `Date` 基于 `gmtime_r`；`Date.parse` / `new Date(string)` 支持 ISO 8601 解析。
- `toLocaleDateString` / `toLocaleTimeString` / `toLocaleString` 按本地时间格式化（`M/D/YYYY`、`h:mm:ss AM`）；支持少量 options（`weekday`/`year`/`month`/`day`/`hour`/`minute`/`second`/`hour12`）。另有 `toDateString` / `toTimeString` / `toUTCString`；无效日期格式化为 `Invalid Date`。

## RegExp

- `RegExp` 在所有宿主上都使用运行时自带的 POSIX ERE 兼容匹配器（`regcomp` / `regexec` 风格），因此结果不依赖平台 libc。捕获组可经 `exec` / `match` / `replace` / `split` / `search` 使用。
- 量词支持 `*`、`+`、`?` 以及计数形式 `{n}`、`{n,}`、`{n,m}`（可带惰性 `?` 后缀）。`{x}`、`{,2}` 这类畸形花括号组保持字面量，与 JavaScript 一致；`{2,1}` 这类倒置范围同样按字面量处理，而不是报错。
- 匹配按字节进行，且 `/g` / `/y` 的 `lastIndex` 不被读取 —— 见[运行时偏差](../unimplemented/deviations.md)。

## Symbol

- `Symbol(description)` 原始值（`XT_OBJECT_KIND_SYMBOL`）、13 个著名符号（`Symbol.iterator`、`Symbol.asyncIterator`、`Symbol.match` 等）、`Symbol.for` / `Symbol.keyFor` 全局注册表、`symbol.description` / `toString()` / `valueOf()`。
- symbol 可作为属性键（`Object.getOwnPropertySymbols`，symbol 键的 `get`/`set`/`in`/`delete`），会被 `Object.keys` / `values` / `entries` / `for...in` / `JSON.stringify` 跳过，打印为 `Symbol(desc)`。

## URL

- `URL` 实现在 `runtime/xt_url_object.c`：支持 `new URL(input)` 与
  `new URL(input, base)`（base 也可以是另一个 `URL`），并按层级式
  `scheme://authority/path` base 解析相对引用。
- **没有协议的引用按文件路径处理**：`new URL("./model.glb")` 相对工作目录解析，
  得到 `file:` URL（显式 base 仍然生效，因此 `new URL("./x", import.meta.url)`
  也可用）。
- 只读属性 `href`、`protocol`、`origin`、`host`、`hostname`、`port`、
  `pathname`、`search`、`hash`、`searchParams` 以及 `toString()` / `toJSON()`；
  绝对输入按原样保留，但 `file:` URL 的 Windows 写法（`file://C:/dir` 与
  `file://C:\dir`）会规范化为 `file:///C:/dir`，与 Node 一致。
- 不是完整的 WHATWG 解析器：未实现路径百分号编码、IDNA、setter 与
  `searchParams` 的实时同步 —— 见[运行时偏差](../unimplemented/deviations.md)。

## URLSearchParams

- `URLSearchParams` 实现在 `runtime/xt_url.c`。

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

## 全局 crypto

- `crypto.getRandomValues(typedArray)` 从操作系统 CSPRNG 填充整数类型化数组（最多 65 536 字节）并返回；`crypto.randomUUID()` 返回 v4 UUID 字符串。`crypto` 也是一等对象，方法可脱离对象使用（`const uuid = crypto.randomUUID`）。
- `Math.random` 使用由操作系统 CSPRNG 播种的 xoshiro256** 流（53 位随机性），不再使用 C `rand()`。
- Node 的 `crypto.randomBytes` / `randomInt` / `randomFillSync` / `pbkdf2Sync` 见 [node/crypto](../implemented/node/crypto.md)。
