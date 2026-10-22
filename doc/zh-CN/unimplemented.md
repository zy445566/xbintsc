# xbintsc 未实现语法与功能

> 语言 / Language：[English](../unimplemented.md) | **简体中文**

本文档基于对源码（`src/`、`runtime/`）与测试（`tests/`）的逐文件核对整理，列出**当前未实现 / 仅部分实现 / 语义与标准不符**的语法与功能。

判定标准（按严重程度）：

1. **未解析**：解析器直接报语法错误，无法进入代码生成。
2. **解析但代码生成报错**：AST / 绑定能建立，但 `codegen` 报 `UnsupportedFeature`。
3. **解析且能编译，但语义未实现或有偏差**：能产出二进制，但运行结果不符合 ECMAScript / TypeScript 语义。

> 近期已补齐：**ECMAScript 语义 + 差分测试** —— 数字格式化 / 强制转换（`toFixed` / `toPrecision` / `toExponential`、十六 / 八 / 二进制解析、完整的 `+` 与关系运算 `ToPrimitive`）、`JSON.stringify` 省略规则、`Array.prototype.sort` 默认稳定字符串序、整数键插入序、整链可选链短路、`finally` 在提前 `return` / `break` / `continue` 时执行、对象键插入序、感知 UTF-8 且能展示循环引用的 `console` 输出、`Array.prototype.splice`、`String.prototype.match` / `lastIndexOf` / 带 limit 的 `split`、正则捕获组（`exec` / `match` / `replace` / `split` / `search`）、不可变数组方法（`toReversed` / `toSorted` / `toSpliced` / `with`）、`Object.getOwnPropertyNames` / `groupBy`、全局 URI 函数、标签语句、`#private` 字段 / 方法 / 静态成员、标记模板（含 raw 字符串与 `String.raw`）、相对模块的 `import * as ns`。测试套件新增了**差分测试脚手架**：每个用例同时经 xbintsc 与 Node 运行并逐字节比较输出。

> 更早已补齐：`class` 声明 / 类表达式、`new` / `this`、继承 `extends` / `super`、`instanceof`、方法 / 静态成员 / 实例字段 / getter 与 setter / 构造器参数属性、`async` / `await` + `Promise`、`import` / `export` 多文件打包（含 `.js` → `.ts` 说明符解析与命名空间导入）、`Map` / `Set` / `Date` / `RegExp` / `JSON` / `BigInt`、解构绑定与解构参数、`enum` / `const enum`，以及**自举**（产出的 LLVM IR 达到逐字节不动点）。

---

## 1. 语句层未实现

### 1.1 解析但代码生成报 `UnsupportedFeature`

| 语法 | 现状 | 说明 |
| --- | --- | --- |
| `namespace` / `module` 声明 | 解析 ✓，代码生成 ✗ | → "does not yet support this statement (module declaration)" |

> 标签语句（`label: statement`、`break label`、`continue label`）已实现，包括非循环语句上的标签。

### 1.2 解析器不支持（直接语法错误）

目前未知有语句级别的缺口。解析器有意保持宽松，接受绝大多数 TypeScript 语句语法；不支持的形式会在代码生成阶段报错（见上）。

---

## 2. 表达式层未实现

### 2.1 解析但代码生成报 `UnsupportedFeature`

无——生成器已实现。

> 标记模板已实现，含 raw 字符串与 `String.raw`。生成器（`function*`、`yield`、`yield*`）
> 基于有栈协程实现，支持 `next`/`throw`/`return`、`for...of`、展开与委托；
> `import.meta` 提供 `url`、`dirname`、`filename`，由正在运行的可执行文件路径解析。
> `async function*` 现在会以 `UnsupportedFeature` **明确拒绝**，不再静默产出同步生成器。

### 2.2 解析器不支持

目前未知有表达式级别的缺口。

### 2.3 运算符未实现

| 运算符 | 现状 | 说明 |
| --- | --- | --- |
| `new.target` | ✗ | 已解析，但以 `UnsupportedFeature` 明确拒绝（无 `new.target` 语义） |
| `typeof` / `void` / `in` / `delete` / `instanceof` | ✓ | 映射到运行时辅助函数（`xt_typeof`、`xt_in`、`xt_delete`、`xt_instance_of`） |

---

## 3. 标准库未实现

### 3.1 已实现（概要）

- 数组：`push` `pop` `shift` `unshift` `join` `slice` `splice` `indexOf` `lastIndexOf` `includes` `concat` `reverse` `forEach` `map` `filter` `reduce` `reduceRight` `find` `findIndex` `findLast` `findLastIndex` `some` `every` `sort` `flat` `flatMap` `fill` `copyWithin` `at` `keys` `values` `entries`，以及不可变的 `toReversed` / `toSorted` / `toSpliced` / `with`
- 字符串：`charAt` `charCodeAt` `codePointAt` `indexOf` `lastIndexOf` `includes` `startsWith` `endsWith` `slice` `substring` `substr` `split` `match` `replace` `replaceAll` `search` `toUpperCase` `toLowerCase` `trim` `trimStart` `trimEnd` `padStart` `padEnd` `repeat` `concat` `at` `localeCompare` `valueOf`
- `Math`：完整函数与常量
- `Object.keys` / `values` / `entries` / `assign` / `getOwnPropertyNames` / `groupBy`，对象展开 `{...obj}`
- 全局函数：`parseInt` `parseFloat` `isNaN` `isFinite` `Number` `String` `Boolean` `encodeURI` `decodeURI` `encodeURIComponent` `decodeURIComponent`
- `console.log` / `info` / `warn` / `error` / `dir` / `trace` / `assert` / `count` / `group` / `table` / `time`

### 3.2 已补齐

- `JSON.parse` / `JSON.stringify`
- `Date`、`RegExp`（POSIX ERE 子集，支持捕获组）、`Map`、`Set`、`Promise`、`BigInt`
- `Array` 静态（`isArray/of/from`）、`Object` 静态、`Number` 静态、`String` 静态（`fromCharCode` / `fromCodePoint` / `raw`）
- 一等公民内建构造器（`const A = Array`、`new A(...)`、`A.isArray`、`x instanceof A`）与 `Object.prototype`（`hasOwnProperty`、`toString`、`valueOf`、`isPrototypeOf`、`propertyIsEnumerable`、`__proto__`），且 `Object.getPrototypeOf({}) === Object.prototype`
- `Error` 家族（`Error`、`TypeError`、`RangeError`、`SyntaxError`、`ReferenceError`、`EvalError`、`URIError`、`AggregateError`）：支持 `new`、`instanceof`、`extends`、`String(err)` / `err.toString()` 与 `.stack`

### 3.3 仍未实现

| 类别 | 现状 |
| --- | --- |
| `Symbol` 构造器与 symbol 原始值 | ✓ `Symbol(description)`、著名符号、`Symbol.for` / `Symbol.keyFor`、symbol 属性键与 `Object.getOwnPropertySymbols` |
| `String.prototype.normalize` | ✗ 未实现 |
| `structuredClone` | ✓ 数组、对象、Map、Set、Date、RegExp 与类型化数组（保留循环引用与共享引用）；函数、symbol、Promise、生成器会被拒绝 |
| `JSON.stringify` 选项 | ~ 已支持 `space`（数字或字符串，最多 10 个单位）；函数/数组形式的 `replacer` 仍被忽略 |
| 内置错误子类（`TypeError`、`RangeError` 等） | ✓ 一等公民构造器与原型；整个家族 `instanceof Error` 成立 |
| `AggregateError` 构造器 | ✓ `new AggregateError(errors, message)`；`Promise.any` 现以其作为 rejection |
| 迭代器协议 / `Symbol.iterator` / `for...of` 自定义可迭代 | ✓ 数组、字符串、`Map`、`Set`、生成器以及任何暴露 `[Symbol.iterator]()` 的对象均可在 `for...of` / 展开中使用 |
| 生成器（`function*`、`yield`、`yield*`、`next`/`throw`/`return`） | ✓ 基于有栈协程实现；支持 `for...of`、展开与委托。`return()` 直接结束，不执行 `finally` |
| 定时器 | ✓ 核心事件循环提供 `setTimeout` / `clearTimeout` / `setInterval` / `clearInterval`（返回数字 id，无 `Timeout` 对象；无 `setImmediate` / `queueMicrotask`）。定时器 settle 的 promise 可以被 await：async 函数体在 pending promise 上挂起，定时器触发时恢复 |
| I/O / 进程等宿主 API | 仅通过扩展（如 Node `fs`）提供 |

### 3.4 类型化数组

已实现：`Uint8Array`、`Int8Array`、`Uint8ClampedArray`、`Uint16Array`、
`Int16Array`、`Uint32Array`、`Int32Array`、`Float32Array`、`Float64Array`，支持
`new X(n)` / `new X(数组样)`、`X.from` / `X.of`、`BYTES_PER_ELEMENT`、按类型强制的
元素写入，以及 `fill` / `set` / `slice` / `subarray` / `join` / `toString` /
`indexOf` / `lastIndexOf` / `includes` / `forEach` / `map` / `filter` / `every` /
`some` / `find` / `findIndex` / `reduce` / `reverse` / `sort` / `copyWithin` /
`at` / `keys` / `values` / `entries`。已知差异：

| 功能 | 现状 |
| --- | --- |
| `subarray` | ~ 返回副本，而非共享缓冲区上的视图 |
| `keys` / `values` / `entries` | ~ 返回数组，而非迭代器对象 |
| `ArrayBuffer` / `DataView` / `.buffer` / `.byteOffset` 语义 | ~ 已有 `ArrayBuffer` 与 `DataView`（默认大端、`slice`、`isView`）；类型化数组仍无共享缓冲区，也不暴露 `.buffer` |
| `BigInt64Array` / `BigUint64Array` | ✗ 未实现 |
| `instanceof` 内建构造器 | ✓ `instanceof Array` / `Object` / `Map` / `Set` / `Date` / `RegExp` / `Promise` / `Error` 家族 / 类型化数组都能按名字识别，**也能通过一等公民构造器值判断**（`const A = Array; x instanceof A`）；构造器可保存、传参、取别名、调用与 `new` |
| 基于 `Buffer` / `ArrayBuffer` 的类型化数组视图 | ✗ 构造器仅能从长度 / 数组样 / 类型化数组复制；`DataView` 可以共享 `ArrayBuffer` 的字节 |

---

## 4. 模块系统

| 功能 | 现状 |
| --- | --- |
| 多文件 / 模块解析与链接 | ✓ 驱动层 AST 打包（`src/driver/modules.ts`）：解析每个模块、按模块前缀重命名顶层符号、改写引用后合并为单文件重新绑定 |
| 具名导入导出 | ✓ `import { a, b as c }` / `export { a as b }` / `export const/let/var/function/class` |
| 默认导入导出 | ✓ `export default` / `import d from` |
| 再导出 `export { x } from` / `export * from` | ✓（`export *` 复制依赖模块的导出） |
| 命名空间导入 `import * as ns` | ✓ 降级为持有全部导出的合成对象字面量 |
| 循环依赖 | ✗ 直接报错（不做循环初始化语义） |
| 实时绑定（live bindings） | ✗ 命名空间对象与导入绑定是模块求值时的快照 |
| ESM `node_modules` 包 | ✓ 裸说明符沿目录树解析，支持 `exports` / `module` / `main`、作用域包与子路径；包源码与相对模块一样被打包 |
| CommonJS `require` / `module.exports` | ✓ 仅限 `node_modules` 内：打包器为每个 CommonJS 包注入合成的 `module` / `exports`，将 `require("x")` 降级为依赖的导出对象，并把外部/内置 `require` 提升为 ESM `import`。用户代码（`node_modules` 之外）的 `require()` 仍会报错并提示改用 ESM `import` |

> 扩展模块（如 `fs`）可通过裸名称或 `node:` 前缀的 `import` 引入 —— `import { readFileSync } from "fs"` / `import path from "path"` —— 并解析到运行时入口（具名、默认与命名空间形式均可）。相对模块与 `node_modules` 中的 ESM/CJS 包均在驱动层打包；CJS 包内的 `require` 会静态解析并内联。

---

## 5. 函数特性未实现（或语义缺失）

| 特性 | 解析 | 语义 |
| --- | --- | --- |
| 默认参数 `function f(a = 5)` | ✓ | ✓ 已实现 |
| 剩余参数 `function f(...args)` | ✓ | ✓ 已实现 |
| 调用参数展开 `f(...args)` | ✓ | ✓ 已实现 |
| 解构参数 / 解构绑定 | ✓ | ✓ 已实现 |
| `arguments` 对象 | ✓（隐式） | ✓ 已实现（箭头函数取的是自身参数，而非外层函数的 `arguments`，与 JS 不同） |
| `this` 绑定 / 方法调用语义 | ✓ | ✓ 已实现（`this` 作为函数首个 ABI 参数线程化；箭头函数词法继承） |
| `async` / `await` / Promise | ✓ | ✓ 已实现：基于栈式协程的真实挂起；等待 pending promise 会暂停函数体并由事件循环恢复（剩余差异见偏差表） |
| `new.target` | ✗ | 已解析，但以 `UnsupportedFeature` 明确拒绝（无 `new.target` 语义） |
| 生成器 / 迭代器 / `yield` | ✓ 已实现 | `function*`、`yield`、`yield*`、`.next/.throw/.return`；异步生成器以 `UnsupportedFeature` 拒绝 |
| 闭包 `arity` | — | `xt_closure_arity` 恒为 -1，未填充 |
| `fn.call` / `fn.apply` / `fn.bind` | ✓ | ✓ 已实现（绑定闭包不跟踪部分参数的 `length`） |
| `fn.name` / `fn.length` | ✓ | ✓ 已实现（从声明 / 赋值 / 属性键推断；绑定函数为 `"bound ..."` 并调整 arity） |
| 内置方法一等公民（`typeof arr.map`、`const f = arr.push`、`obj.method?.()`） | ✓ | ✓ 作为**未绑定**方法值实现：读取 `arr.map` 得到函数，`arr.map?.(cb)` 与 `obj.method?.()` 会把接收者作为 `this` 绑定，脱离接收者调用（`const f = arr.map; f(cb)`）会像 JavaScript 一样抛错。方法的 `name`/`length` 也一并暴露 |
| 内建构造器一等公民（`const A = Array`、`new A(...)`、`typeof Map`） | ✓ | ✓ 构造器是普通函数值，带 `name`/`length`、`prototype`、静态成员（`A.isArray`）与完整的 `instanceof` 协议 |
| `fn.toString()` | ~ | 返回原生占位字符串（`function name() { [native code] }`），而非原始源码文本 |

---

## 6. 类与面向对象

| 特性 | 现状 |
| --- | --- |
| `class` 声明 / 类表达式 | ✓ 已实现 |
| 构造函数 `constructor` | ✓ |
| 实例字段 / 属性声明 | ✓ |
| 方法 | ✓ |
| `static` 字段 / 方法 | ✓ |
| 继承 `extends` / `super` | ✓（单级正确；`super` 取 `this` 原型的原型，理论缺陷见第 8 节） |
| 原型链 / 方法查找 | ✓ |
| `instanceof` | ✓ |
| `new` / 实例化 | ✓ |
| `get` / `set` 访问器 | ✓ 已实现 |
| 参数属性 `constructor(public x: T)` | ✓ 已实现 |
| 私有字段 `#x` | ✓ 已实现（以字面量 `#x` 作为键存储；无访问控制强制） |
| `enum` / `const enum` | ✓ 已实现（正向 + 反向映射） |
| `private` / `protected` / `public` / `readonly` 修饰符 | ✗ 无访问控制（擦除） |
| `abstract` / `implements` | ✗（擦除） |
| 父子类 `#x` 同名 | ✗ 可能混用（同一字面量键） |

---

## 7. 类型系统未实现（仅解析、不检查）

类型语法被解析为 AST 后在绑定 / 代码生成阶段**整体擦除**，不做任何类型检查：

- 类型注解、返回类型、类型参数、类型别名、接口、泛型约束等：解析 ✓，检查 ✗。
- 检查器诊断码（`TypeMismatch` `NotCallable` `PropertyNotFound` `ArgumentCountMismatch`）已定义但**未使用**。
- `as` / `satisfies` / 非空断言 `!`：直接擦除，不做任何断言语义。
- 泛型：无运行时实例化，类型参数被忽略。
- 可选链 `?.` 的类型窄化：无（运行时短路已实现，但无类型层面的窄化）。

---

## 8. 运行时 / 语义与标准不符（已知偏差）

这些特性**能编译、能运行**，但结果不完全符合 ECMAScript：

| 项 | 偏差 |
| --- | --- |
| 正则按字节匹配 | 引擎按 UTF-8 字节匹配：多字节字面量仍可匹配，但 `.` 与字符类每次只消费一个字节（`\d`/`\w` 仍是 ASCII）。`exec` / `search` / 替换回调返回的偏移会转换为 UTF-16 码元 |
| 全局正则 `lastIndex` | `/g`、`/y` 正则的 `test` / `exec` 不推进也不读取调用方设置的 `lastIndex` |
| `String.normalize` | 未实现 |
| 内置方法一等公民 | 以未绑定方法值实现（见第 5 节）；`fn.toString()` 返回占位字符串而非源码文本 |
| `for...in` | 枚举对象 / 数组 / 字符串的自身键；不含原型链属性 |
| 数组越界 / 稀疏 | 越界访问返回 `undefined`；对 `arr.length` 赋值会截断 / 扩展，但不区分稀疏空洞 |
| 内存管理 | 非移动标记-清扫回收器（在 `xt_alloc` 之后）：显式根、子系统根提供者与保守 C 栈扫描。单线程、stop-the-world；无移动/压缩与分代 |
| 函数 `arity` / 调用参数个数 | 无参数个数校验；`fn.length` 报告声明的 arity，但调用不做校验 |
| `async` / `await` | 已在栈式协程上真实挂起；`await` 总会排入一次微任务（非 Promise 与已 settle 的 Promise 也一样），因此与 pending `.then` 回调的交错顺序符合规范。顶层 `await` 会阻塞事件循环等待，而不是让出模块主体。`fs/promises` 仍在 settle 前同步完成 I/O；`fetch` 是阻塞的；不支持 `async` 生成器 |
| `super` | `super.x` / `super(...)` 取 `this` 原型的原型；单级继承正确，继承深度 > 1 时可能不准确 |
| `Error.stack` | 仅函数级帧：每帧为 `at 名称 (文件:行)`，行号取自函数声明（最内层帧会更新为错误构造处）；没有列号，生成器 / `async` 函数体不产生帧（它们会切换栈） |
| 模块实时绑定 | 命名空间导入与导入绑定为快照（见第 4 节） |
| `import.meta` | ✓ `import.meta.url` / `.dirname` / `.filename`，由可执行文件路径解析 |

> 数字格式化、宽松相等、`+` / 关系运算 `ToPrimitive`、整链可选链、`finally` 提前退出、对象键顺序等均已对齐 Node，并由差分测试覆盖，故不再列为偏差。

---

## 9. 工具链 / 平台 / 工程未实现

| 项 | 现状 |
| --- | --- |
| GC（垃圾回收） | ✓ `xt_alloc.c` 中的非移动标记-清扫，按存活堆阈值（`XT_GC_THRESHOLD` 可固定该阈值）或 `xt_gc_collect()` 触发；每次回收后阈值变为存活堆的 2 倍（下限 1 MiB），因此常驻内存跟随存活集而不是只增不减；`XT_GC_TRACE=1` 会为每次回收向 stderr 打印一行统计。显式根 + 根提供者 + 保守活动栈扫描。无压缩、弱引用或后台回收（缺少弱引用意味着扩展必须对它缓存的每个值做强引用，例如 GUI 的元素句柄） |
| 自举（self-hosting） | ✓ 编译器已能自编译：`xbintsc build src/cli/main.ts` 可产出可用二进制，且从第 1 代起产出的 IR 保持稳定。运行时仍为 C |
| 类型检查器 | ✗ 仅定义诊断码，无 checker |
| 完整标准库 | 部分：Math / JSON / Date / Map / Set / RegExp / `Error` / `BigInt` / `Symbol` / `structuredClone` 已实现；String.normalize 缺失 |
| 多文件模块打包 | 部分：相对路径 `.ts` 打包、ESM `node_modules` 包、命名空间导入、裸说明符扩展模块导入已实现；循环依赖 / CommonJS / 实时绑定未实现 |
| 异步运行时 | ✓ `async`/`await` 在 `select(2)` reactor 上对 pending promise 挂起：定时器、TCP 套接字与 promise 回调都能恢复协程。仍缺：真正的异步 I/O 调度（`fs/promises` 会先阻塞完成 I/O 再 settle）、非阻塞 `fetch` 与 worker 线程（worker 是子进程） |
| Windows 二进制产物验证 | 构建层已适配（`.exe` 后缀、链接参数分支），并在 CI 中验证 |
| 精确的 ECMAScript 数值 / 字符串 / 比较语义 | 部分，见第 8 节 |

---

## 10. 速查：未实现 / 部分实现清单

```
未实现（语句）：namespace/module 声明

未实现（表达式）：new.target（会给出明确的 UnsupportedFeature）

未实现（函数）：`fn.toString()` 源码文本、异步生成器（已明确拒绝）

未实现（类/面向对象）：abstract/implements、访问控制、父子类 #x 同名

未实现（标准库）：String.normalize

未实现（模块）：循环依赖、实时绑定、动态 `require(expr)`、
                     `require.resolve` / `require.cache`、`__dirname` / `__filename`

未实现（类型系统）：类型检查、泛型实例化、断言语义、可选链类型窄化

未实现（运行时）：异步 I/O 调度、
                  全局正则 lastIndex

未实现（工程）：类型检查器
```
