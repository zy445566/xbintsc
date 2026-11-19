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

- `globalThis.x` 与 `global.x` 读取**已知**全局名时按裸全局名处理（`globalThis.fetch`、
  `globalThis.Deno`）；其他属性则是全局对象上的普通读取，因此 `globalThis.missing` 为
  `undefined`，`global[key] = v` 也能读回。
- `globalThis`（以及 Node 的别名 `global`）是值：两种写法共享同一个普通属性袋，所以
  `const g = globalThis`、`globalThis.x = 1` 以及用 symbol 键在 `global` 上发布状态都可用。
  它不是运行时作用域对象——库用 `global.foo = ...` 写入的名字，若编译器不认识，读取仍是
  `undefined`。
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
- `Math`：完整函数与常量。`Math.hypot(...values)` 接受任意个参数（ECMA-262 21.3.2.18）：每个参数都参与计算，出现 `NaN` 即为 `NaN`，并按最大幅值缩放求和，因此 `hypot(1e308, 1e308)` 不会溢出。
- `Math` 调用点先按成员名的首字节分派，再比较完整名字，因此数值循环里的 `Math.imul` / `floor` / `exp` 不再为一条线性 `strcmp` 链付费（程序化纹理与几何代码会调用它们数亿次；去掉这条链后，打包 three.js 页面的关卡初始化时间减半）。
- `Math` 同时是一等对象：`const cos = Math.cos`、`[1, 2].map(Math.max)`、`const { sqrt, PI } = Math` 都能按值读取成员（`xt_math_object()` 为每个函数缓存一个原生闭包，读取开销仍然很低）。
- `Number` 静态与 `Number()` 转换；`parseInt` / `parseFloat` / `isNaN` / `isFinite`。

## JSON

- `JSON.parse` 支持 `reviver`；畸形输入（错误数字/字符串、尾随内容、缺少分隔符）会抛出可捕获的 `SyntaxError`，而不是返回 `undefined`。
- `JSON.stringify` 支持函数或数组 `replacer`、`toJSON`，以及 `space`（数字或字符串，最多 10 个单位）。`toJSON` 对每个对象都会调用，并接收属性名（顶层为空字符串 `""`，数组元素为其下标），且先于其他判断执行，因此 `toJSON` 返回 `undefined` 的值在对象中被省略、在数组中变为 `null`。`undefined` / 函数 / symbol 值遵循标准省略规则。
- 对象键按插入序输出，整数键按数值排序；循环引用抛错。

## Date

- `Date` 基于 `gmtime_r`；`Date.parse` / `new Date(string)` 支持 ISO 8601 解析。时间不可用（NaN）时 `toJSON` 返回 `null` 而不是抛错。
- `toLocaleDateString` / `toLocaleTimeString` / `toLocaleString` 按本地时间格式化（`M/D/YYYY`、`h:mm:ss AM`）；支持少量 options（`weekday`/`year`/`month`/`day`/`hour`/`minute`/`second`/`hour12`）。另有 `toDateString` / `toTimeString` / `toUTCString`；无效日期格式化为 `Invalid Date`。

## RegExp

- `RegExp` 在所有宿主上都使用运行时自带的 POSIX ERE 兼容匹配器（`regcomp` / `regexec` 风格），因此结果不依赖平台 libc。捕获组可经 `exec` / `match` / `replace` / `split` / `search` 使用。
- 量词支持 `*`、`+`、`?` 以及计数形式 `{n}`、`{n,}`、`{n,m}`（可带惰性 `?` 后缀）。`{x}`、`{,2}` 这类畸形花括号组保持字面量，与 JavaScript 一致；`{2,1}` 这类倒置范围同样按字面量处理，而不是报错。
- JavaScript 正则先翻译给匹配器（`\d`/`\w`/`\s` → 字符类、`(?:` → 捕获组、控制字符转义 → 字面量）。其余转义都是**字面字符**并保留反斜杠，因此 `\[`、`\]`、`\(`、`\.`、`\+` 匹配对应字符而不会开启语法：`"[ i ]".replace(/\[\s*i\s*\]/g, "Z")` 与 Node 一致。three.js 的 `unrollLoops` 正依赖这个形状。
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

- `URLSearchParams` 实现在 `runtime/xt_url.c`。它是可迭代的（`[Symbol.iterator]`，因此 `[...params]`、`for (const [k, v] of params)` 与 `Object.fromEntries(params)` 都可用）；`Object.fromEntries` 接受任意可迭代对象——键值对数组、`Map`、`URLSearchParams` 或生成器——而不再只接受数组。

## 错误

- `Error` 家族：`Error`、`TypeError`、`RangeError`、`SyntaxError`、`ReferenceError`、`EvalError`、`URIError`、`AggregateError` —— 支持 `new`、`instanceof`、`extends`、`String(err)` / `err.toString()` 与 `.stack`。
- `Promise.any` 以 `AggregateError` 作为 rejection。
- `.stack` 仅含函数级帧（`at 名称 (文件:行)`），取自运行时影子调用栈；具体限制见[运行时偏差](../unimplemented/deviations.md)。

## console

- `xt_print` / `xt_println` / `xt_console_log` / `info` / `warn` / `error`；`info` / `log` 到 stdout，`warn` / `error` 到 stderr。
- `console` 既是命名空间，也是可当作值使用的一等对象：`const log = console.log`、`const { warn } = console`、`console.error.bind(console)`、`console.error = ...` 都能工作 —— 读取 `console` 会返回一个缓存的对象，其方法都是普通函数（`xt_console_object`）。调用点仍然是直接调用（`console.log(...)` 会降低为运行时符号），因此程序不把它当值用时没有额外开销。
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

## Proxy

`new Proxy(target, handler)` 以宿主视图实现（`runtime/xt_proxy.c`）：代理对象的自有属性袋
始终为空，因此每个字符串键查找都会落空并进入 handler。

- 支持的 trap：`get(target, key, receiver)`、`set(target, key, value, receiver)`、
  `has(target, key)`、`deleteProperty(target, key)`、`ownKeys(target)`。缺失的 trap
  把操作转发给 target，所以 `new Proxy(obj, {})` 与 `obj` 行为一致（含写入与删除）。
- `Object.keys` / `JSON.stringify` / `for...in` 使用 `ownKeys` 的结果；读取这些键会走
  `get`。`typeof` 为 `"object"`，代理继承 target 的原型，因此继承方法与 `instanceof` 可用。
- 未建模：函数 target（`apply` / `construct`）、`Proxy.revocable`、原型与属性描述符类
  trap（`getPrototypeOf`、`setPrototypeOf`、`getOwnPropertyDescriptor`、
  `defineProperty`、`isExtensible` …），以及不变量校验（trap 可以报告 target 上并不存在
  的键）。target 与 handler 以隐藏的 symbol 键属性存放，因此
  `Object.getOwnPropertySymbols(proxy)` 会看到它们。

## Web 平台全局对象

实现位于 `runtime/xt_web_abort.c`（中止）与 `runtime/xt_fetch/webapi.inc`
（`Headers` / `Response`，复用 `fetch` 的实现）：

- `AbortController` / `AbortSignal`：`abort(reason?)`、`aborted` / `reason` /
  `onabort` 访问器、`addEventListener("abort", fn[, { once }])` /
  `removeEventListener`、`throwIfAborted()`，以及静态方法 `AbortSignal.abort()`、
  `AbortSignal.timeout(ms)`、`AbortSignal.any([...])`。默认原因是 `AbortError` /
  `TimeoutError` 错误（看 `error.name`）。`fetch` 尚未消费 signal，因此中止不会取消
  进行中的请求。
- `Headers`：大小写不敏感的 `get` / `has` / `set` / `append` / `delete` /
  `forEach` / `keys` / `values` / `entries` / `getSetCookie`，可由普通对象、键值对数组
  或另一个 `Headers` 构造。`fetch` 返回的就是 `Headers` 实例（`instanceof` 成立）。
- `Response`：`new Response(body?, init?)`，含 `status` / `statusText` / `headers`
  以及 `text()` / `json()` / `arrayBuffer()` / `bytes()` / `clone()`。
  `arrayBuffer()` 解析为真正的 `ArrayBuffer`，`bytes()` 为 `Uint8Array`。
- `JSON`、`ArrayBuffer`、`DataView` 也是值（`const s = JSON.stringify`、
  `ArrayBuffer.prototype`、`new DataView(buffer)`）；`JSON` 保留专用调用降级，同时
  支持作为对象读取。
