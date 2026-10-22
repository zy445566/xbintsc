# 语言支持范围：什么能编译，什么行为不同

xbintsc 编译 TypeScript 的一个**实用子集**。本页用来判断你能写什么；要精确措辞请
点开链接。

权威细节（始终最新、篇幅长得多）：

- [../../zh-CN/implemented.md](../../zh-CN/implemented.md) —— 所有已实现能力，按编译器阶段组织
- [../../zh-CN/unimplemented.md](../../zh-CN/unimplemented.md) —— 不支持的语法、行为偏差、速查表
- [../../zh-CN/node-implemented.md](../../zh-CN/node-implemented.md) / [../../zh-CN/node-unimplemented.md](../../zh-CN/node-unimplemented.md) —— Node 扩展

## 图例

| 标记 | 含义 |
| --- | --- |
| ✅ | 与 Node/TypeScript 一致 |
| ⚠️ | 可用，但与标准行为有偏差 |
| 🚫 | 被拒绝——语法错误或 `UnsupportedFeature` |
| — | 被解析后擦除，运行时无任何作用 |

## 语句与声明

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| `var`/`let`/`const`、块、`if`、`for`、`for...of`、`for...in`、`while`、`do...while` | ✅ | `for...in` 只枚举自有键（不含原型链） |
| `switch`、`break`、`continue`、标签 | ✅ | 含带标签的 `break`/`continue` |
| `try`/`catch`/`finally`、`throw` | ✅ | 提前 `return`/`break`/`continue` 时 `finally` 仍会执行 |
| `function`、箭头函数、默认参数/剩余参数 | ✅ | |
| `namespace`/`module` 声明 | 🚫 | 解析 ✓，代码生成 ✗ |
| `with`、`debugger` | 🚫 | 不支持 |
| 顶层 `await` | ⚠️ | 会在事件循环上阻塞直到 Promise settle（不会像 ES module 那样让出模块主体）；建议包进 `async` 函数 |

## 表达式与运算符

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| 算术、位运算、逻辑、比较、赋值运算符 | ✅ | `+` 与 `ToPrimitive` 强转与 Node 一致 |
| `===`/`!==`/`==`/`!=` | ✅ | 宽松相等与 Node 一致 |
| 可选链 `?.`、空值合并 `??`、逻辑赋值 `??=` | ✅ | 整条链的短路都已实现 |
| 模板字符串、标签模板 | ✅ | 标签模板提供 `raw` 与 `String.raw` |
| 调用/数组/对象字面量中的展开与剩余 | ✅ | |
| 解构（绑定、参数、嵌套） | ✅ | 支持默认值与剩余 |
| `delete`、`in`、`instanceof`、`typeof` | ✅ | |
| 逗号运算符、`void` | ✅ | |
| `new.target` | 🚫 | 能解析，但会给出明确的 `UnsupportedFeature` |
| `import.meta` | ✅ | `url`、`dirname`、`filename`（由可执行文件路径解析） |
| `super` | ⚠️ | 单层继承正确；继承深度 > 1 可能不准 |

## 函数、类与对象

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| 闭包（通过 box 按引用捕获） | ✅ | 直接调用与闭包调用共用一条 ABI |
| `this`、方法调用、箭头函数词法 `this` | ✅ | 与 JS 不同：箭头函数有自己的 `arguments` |
| `call`/`apply`/`bind`、`fn.name`/`fn.length` | ✅ | bind 出的闭包不跟踪部分应用后的 `length` |
| 一等内置方法（把 `arr.map` 当值用） | ✅ | 暴露为*未绑定*方法值；脱离接收者调用时与 JS 一样抛错 |
| `fn.toString()` | ⚠️ | 返回 `function name() { [native code] }`，不是源码 |
| 类：字段、方法、静态成员、getter/setter、`extends`/`super`、`instanceof` | ✅ | |
| 构造器参数属性 `constructor(public x: T)` | ✅ | |
| `#private` 字段/方法/静态成员 | ⚠️ | 以字面量 `#x` 键存储；不做访问控制；父子类同名可能互相覆盖 |
| `enum`/`const enum` | ✅ | 正向 + 反向映射 |
| `private`/`protected`/`public`/`readonly`/`abstract`/`implements` | — | 被擦除，无访问控制 |
| 生成器 `function*`、`yield`、`yield*` | ✅ | 不支持 `async` 生成器 |
| `arguments` 对象 | ✅ | 隐式提供；箭头函数看到的是自己的参数 |
| 参数个数校验 | 🚫 | 从不校验，虽然 `fn.length` 会报声明元数 |

## 异步、Promise 与事件循环

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| `async`/`await`、`Promise`、`Promise.all` | ✅ | 真实挂起：async 函数体运行在栈式协程上，等待**未 settle** 的 Promise 时暂停，settle 后恢复。每次 `await` 都会让出一次，已 settle 的值或非 Promise 也会像 Node 一样排入微任务 |
| `setTimeout` 与回调 | ✅ | 定时器运行在核心事件循环上；`await new Promise(r => setTimeout(r, ms))` 可用 |
| 套接字/服务端（`http`、`net`、`dgram`） | ✅ | 回调在事件循环上执行，可以 settle 你正在 await 的 Promise（测试含 `net`/`http` 示例） |
| `fs/promises` | ⚠️ | 可以 await，但每次调用仍先同步完成 I/O 再 settle，I/O 期间不会让出 |
| `async` 生成器、worker 线程 | 🚫 | `async function*` 被拒绝；worker 是子进程 |

## 类型：解析后擦除

**不做任何类型检查。** 类型语法被解析进 AST，随后在绑定/代码生成阶段擦除：

| 构造 | 状态 |
| --- | --- |
| 类型注解、返回类型、类型别名、接口 | — 解析后擦除，从不检查 |
| 泛型参数与约束 | — 运行时无实例化 |
| `as`、`satisfies`、非空断言 `!` | — 擦除，无断言语义 |
| 可选链的类型收窄 | 🚫 |
| 诊断码 `TS4001`–`TS4004`（`TypeMismatch`、`NotCallable`、`PropertyNotFound`、`ArgumentCountMismatch`） | 🚫 有定义但从不发出 |

因此源码里的类型错误**不是**编译错误——只有运行时行为会被检查。你想跑出什么行为，
就写什么代码。

## 标准库

已实现：`Math`、`JSON`、`Date`、`RegExp`、`Map`、`Set`、`WeakMap`/`WeakSet`、
`Symbol`、`Error` 家族、`BigInt`、`Array`/`String`/`Number`/`Object` 方法、TypedArray、
带捕获组的 `String.prototype.match`/`split`/`replace`、不可变数组方法
（`toReversed`、`toSorted`、`toSpliced`、`with`）、全局 URI 函数、
一等公民内建构造器（`const A = Array; new A(...)`、`A.isArray`、`x instanceof A`）
与 `Object.prototype`（`Object.getPrototypeOf({}) === Object.prototype`）。

| 缺失或行为不同 | 状态 |
| --- | --- |
| `String.prototype.normalize`、`structuredClone` | 🚫 |
| `Error.stack` 采集 | ⚠️ 函数级帧（名称/文件/行号），没有列号；生成器函数体不产生帧 |
| 全局 RegExp `lastIndex` | ⚠️ `/g`、`/y` 下 `test`/`exec` 忽略它 |
| 稀疏数组空洞、数组越界 | ⚠️ 不区分空洞；越界读得 `undefined` |

完整列表见 [../../zh-CN/unimplemented.md](../../zh-CN/unimplemented.md) 第 3 节。

## 模块

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| ESM `import`/`export` | ✅ | |
| 相对路径多文件打包 | ✅ | `./helper.js` 会解析到 `helper.ts` |
| `import * as ns`、默认导入与具名导入 | ✅ | |
| Node 内置模块 | ✅ | 仅在 `--ext node` 下——见 [extensions.md](./extensions.md) |
| 裸第三方 npm 包 | ⚠️ | 从 `node_modules` 解析并作为源码打包（ESM 和 CommonJS 均支持）；运行时不做包查找，循环依赖会被拒绝 |
| `require()` / CommonJS | ⚠️ | 在自己的源码里会被拒绝并提示改用 `import`；`node_modules` 内的 CommonJS 包由打包器处理 |
| 循环依赖 | 🚫 | |
| 实时绑定 | ⚠️ | 导入与命名空间成员都是快照 |
| `__dirname`、`__filename` | 🚫 | 请使用 `import.meta.dirname` / `import.meta.filename` |

## 字符串、数字与内存

| 项目 | 行为 |
| --- | --- |
| `String.prototype.length` | ✅ UTF-16 码元（`"中文😀".length === 4`）；索引、`slice`、`indexOf`、`split`、`padStart`、`codePointAt` 使用同一模型。底层仍以 UTF-8 存储 |
| 正则 Unicode 匹配 | ⚠️ 引擎按字节工作：字面量 `中` 仍能匹配，但 `.` 与字符类每次只消费一个字节，`\d`/`\w` 仍是 ASCII。匹配偏移（`match.index`、`search`、替换回调）按 UTF-16 码元报告 |
| 数字格式化与强转 | ✅ 与 Node 一致（`toFixed`、`toPrecision`、十六/八/二进制解析） |
| `BigInt` | ✅ 已实现 |
| 垃圾回收 | ✅ 非移动标记清扫；显式根 + C 栈保守扫描；单线程、stop-the-world；无弱引用 |
| 线程 | 🚫 单线程；`worker_threads` 只存在于 Node 扩展中 |

## 平台

构建目标覆盖 macOS、Linux、Windows 的 x64 与 arm64。Windows 上是 **MSVC ABI**，
clang 需要 MSVC/SDK 环境——见 [../../zh-CN/requirements.md](../../zh-CN/requirements.md)。
需要 clang **16 或更新**。

## 写大程序之前

1. 扫一眼你要用的特性所在的行；凡标 ⚠️ 或 🚫 的，打开
   [../../zh-CN/unimplemented.md](../../zh-CN/unimplemented.md) 确认。
2. 用 `xbintsc emit app.ts` 做原型验证——这是确认某构造是否被支持最便宜的方式，
   不需要 clang。
3. 用 `xbintsc run app.ts` 跑真实程序；构建失败就去
   [troubleshooting.md](./troubleshooting.md)。
4. 需要精确对齐 Node 行为时，把同一程序在 Node 下跑一遍对比输出；项目自带测试套件
   就是这么做的差分验证。
