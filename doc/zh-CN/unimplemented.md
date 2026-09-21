# xbintsc 未实现语法与功能

> 语言 / Language：[English](../unimplemented.md) | **简体中文**

本文档基于对源码（`src/`、`runtime/`）与测试（`tests/`）的逐文件核对整理，列出**当前未实现 / 仅部分实现 / 语义与标准不符**的语法与功能。

判定标准（按严重程度）：

1. **未解析**：解析器直接报语法错误，无法进入代码生成。
2. **解析但代码生成报错**：AST / 绑定能建立，但 `codegen` 报 `UnsupportedFeature`。
3. **解析且能编译，但语义未实现或有偏差**：能产出二进制，但运行结果不符合 ECMAScript / TypeScript 语义。

> 最新批次：**自举（self-hosting）** —— `xbintsc` 已能用自身编译 `src/cli/main.ts` 生成可运行的原生二进制，且产出的 LLVM IR 达到不动点（源码 ≡ 第 1 代 ≡ 第 2 代 ≡ 第 3 代，逐字节一致）。同时运行期补齐了数组 `length` 赋值（截断 / 扩展）、可迭代对象展开（`[...set]`、`[...map]`、`"abc"`）、对 `Map` / `Set` 的 `for...of`，binder 也补齐了计算属性名（`{ [E.A]: 1 }`）。

> Windows 自举：C 端 `path` 模块现在识别 Windows 原生分隔符（`/` 与 `\`）和盘符前缀（`C:`），输出仍统一为 `/`；`parseInt` 改为累加到 `double`（不再用 `strtol`），因此大整数常量（如 `0xcbf29ce4`、`0x100000000`）不会被 Windows 的 32 位 `long` 截断为 `INT32_MAX`；产出的 `source_filename` 统一为 `/`，保证自举 IR 不动点在各平台逐字节一致。

> 近期已补齐（本批次）：`class` 声明 / 类表达式、`new` / `this`、继承 `extends` / `super`、`instanceof`、方法 / 静态成员 / 实例字段、`async` / `await` + `Promise`（`then/catch/finally`、`resolve/reject/all/allSettled/race`）、`import` / `export` 多文件打包、`Map` / `Set` / `Date` / `RegExp` / `JSON`、以及大量数组 / 字符串 / 数字 / 对象 / 控制台扩展方法。详见 [已实现文档](implemented.md)。

> 更早已补齐：`switch`、`try/catch/finally`、对象展开、`delete`、`in`、数组 / 字符串方法、`Math`、`Object.keys/values/entries/assign`、全局函数、`console.error/warn/info`、`arguments`、默认 / 剩余参数、可选链短路、`for...in` 对象键枚举。

---

## 1. 语句层未实现

### 1.1 解析但代码生成报 `UnsupportedFeature`

| 语法 | 现状 | 说明 |
| --- | --- | --- |
| `enum` 声明 | 解析 ✓，代码生成 ✗ | `enum E { A, B }` → "does not yet support this statement (enum declaration)" |
| `namespace` / `module` 声明 | 解析 ✓，代码生成 ✗ | → "does not yet support this statement (module declaration)" |
| `label: statement` | 解析 ✓，代码生成 ✗ | 无标签跳转语义 |

### 1.2 解析器不支持（直接语法错误）

| 语法 | 现状 | 说明 |
| --- | --- | --- |
| 标签语句 `label: statement`（部分路径） | 部分解析 | `parseStatement` 对部分 `label:` 形式仍可能报 "Unexpected token ':'" |

> `class` / `new` / `this` / `import` / `export` 现已实现，见下。

---

## 2. 表达式层未实现

### 2.1 解析但代码生成报 `UnsupportedFeature`

| 语法 | 现状 | 说明 |
| --- | --- | --- |
| 标记模板 `` f`...` `` | 解析 ✓，代码生成 ✗ | → "does not yet support this expression (tagged template)" |
| `yield` 表达式（生成器） | 解析 ✓，代码生成 ✗ | → "does not yet support this expression (yield expression)" |
| 数组解构 / 对象解构 | 解析 ✓，代码生成 ✗ | 解构绑定与解构赋值均未实现 |

### 2.2 解析器不支持

| 语法 | 现状 | 说明 |
| --- | --- | --- |
| 正则表达式字面量作为表达式 | 未解析 | scanner 能扫描 `/re/`，但 `parsePrimaryExpression` 无 `RegularExpressionLiteral` 分支，报 "Unexpected token '/…/'";可使用 `new RegExp(...)` |
| `Promise.any` / 类型关键字作属性名（如 `.any`、`.get` 部分场景） | 部分未解析 | 上下文关键字作成员名时偶发 "Expected identifier" |
| 函数调用参数展开 `f(...args)` / `Math.max(...xs)` | 未解析 | `SpreadElement` 在调用参数位置报 unsupported |

### 2.3 运算符未实现（代码生成报错）

| 运算符 | 现状 | 说明 |
| --- | --- | --- |
| `typeof` / `void` | 已实现 | — |
| `instanceof` | 已实现 | 映射 `xt_instance_of` |

> 值级 `typeof` / `void` 已实现（见已实现文档）。`in`、`delete` 已实现。

---

## 3. 标准库未实现

### 3.1 已实现（概要）

- 数组：`push` `pop` `shift` `unshift` `join` `slice` `indexOf` `includes` `concat` `reverse` `forEach` `map` `filter` `reduce`
- 字符串：`charAt` `charCodeAt` `indexOf` `includes` `slice` `substring` `substr` `split` `toUpperCase` `toLowerCase` `trim` `replace` `repeat` `startsWith` `endsWith` `concat`
- `Math`：`abs` `floor` `ceil` `round` `trunc` `sqrt` `cbrt` `pow` `exp` `log` `log2` `log10` `sin` `cos` `tan` `asin` `acos` `atan` `atan2` `hypot` `sign` `random` `min` `max`，常量 `PI` `E` `LN2` `LN10` `LOG2E` `LOG10E` `SQRT2` `SQRT1_2`
- `Object.keys` / `values` / `entries` / `assign`，对象展开 `{...obj}`
- 全局函数：`parseInt` `parseFloat` `isNaN` `isFinite` `Number` `String` `Boolean`
- `console.log` / `info` / `warn` / `error`

### 3.2 已补齐（本批次）

- `JSON.parse` / `JSON.stringify`
- `Date`（构造、`getTime`、`getFullYear`/`getUTCFullYear` 等、`toISOString`/`toJSON`）、`RegExp`（`new RegExp`、`test`、`exec`，POSIX ERE 子集）
- `Map`（`set/get/has/delete/clear/size`）、`Set`（`add/has/delete/clear/size`）
- `Promise`（`resolve/reject/all/allSettled/race`、实例 `then/catch/finally`）
- `Array` 静态（`isArray/of/from`）、`Object` 静态（`keys/values/entries/assign/freeze/isFrozen/fromEntries/getPrototypeOf/setPrototypeOf/hasOwn/is/create`）、`Number` 静态（`isInteger/isSafeInteger/isFinite/isNaN/parseInt/parseFloat` 及常量）、`String` 静态（`fromCharCode/fromCodePoint/raw`）
- 数组实例扩展：`at/find/findIndex/findLast/findLastIndex/some/every/sort/flat/flatMap/lastIndexOf/fill/copyWithin/reduceRight/toString/keys/values/entries`
- 字符串实例扩展：`at/padStart/padEnd/trimStart/trimEnd/replaceAll/localeCompare/codePointAt/valueOf`
- 数字实例：`toFixed/toPrecision/toExponential/toString(radix)/valueOf`
- `Math` 扩展：`log1p/expm1/sinh/cosh/tanh/asinh/acosh/atanh/fround/imul/clz32`
- `console` 扩展：`dir/trace/assert/count/countReset/group/groupEnd/table/time/timeEnd/timeLog`

### 3.3 仍未实现

| 类别 | 现状 |
| --- | --- |
| `Symbol` 构造器与 symbol 原始值 | ✗ 未实现 |
| `BigInt` 任意精度 | ✓ 已实现（符号-数值大整数：`+ - * / % **`、位运算、移位、比较、`toString(radix)`、`BigInt()` / `BigInt.asIntN` / `BigInt.asUintN`，支持 `0x` / `0o` / `0b` 字面量） |
| `Error` 构造器 / `message` / `stack` | ✓ 已实现 `new Error(...)` / `extends Error`；其余错误子类（`TypeError` 等）尚未实现 |
| 定时器 / I/O / 进程等宿主 API | 仅通过扩展（如 `node` fs）提供 |
| 迭代器协议 / `Symbol.iterator` / `for...of` 自定义可迭代 | 部分：数组、字符串、`Map`、`Set` 均可在 `for...of` / 展开中使用；不读取自定义 `Symbol.iterator` |
| 生成器 / 异步迭代 | ✗ 未实现 |

---

## 4. 模块系统（部分实现）

| 功能 | 现状 |
| --- | --- |
| 多文件 / 模块解析与链接 | ✓ 驱动层 AST 打包（`src/driver/modules.ts`）：解析每个模块、按模块前缀重命名顶层符号、改写引用后合并为单文件重新绑定 |
| 具名导入导出 | ✓ `import { a, b as c }` / `export { a as b }` / `export const/let/var/function/class` |
| 默认导入导出 | ✓ `export default` / `import d from` |
| 再导出 `export { x } from` / `export * from` | ✓（`export *` 为近似复制） |
| 命名空间导入 `import * as ns` | ✗ 相对模块未实现（扩展模块如 `path` 已支持：`import * as path from "path"`） |
| 循环依赖 | ✗ 直接报错（不做循环初始化语义） |
| 第三方 / npm 依赖 | ✗ 未实现（仅相对路径 `.ts` 文件） |

> 扩展模块（如 `fs`）可通过裸名称或 `node:` 前缀的 `import` 引入 —— `import { readFileSync } from "fs"` / `import path from "path"` —— 并解析到运行时入口（具名、默认与命名空间形式均可）。相对模块仍在驱动层打包。

---

## 5. 函数特性未实现（或语义缺失）

| 特性 | 解析 | 语义 |
| --- | --- | --- |
| 默认参数 `function f(a = 5)` | ✓ | ✓ 已实现 |
| 剩余参数 `function f(...args)` | ✓ | ✓ 已实现 |
| `arguments` 对象 | ✓（隐式） | ✓ 已实现（箭头函数取的是自身参数，而非外层函数的 `arguments`，与 JS 不同） |
| `this` 绑定 / 方法调用语义 | ✓ | ✓ 已实现（`this` 作为函数首个 ABI 参数线程化；箭头函数词法继承） |
| `async` / `await` / Promise | ✓ | ✓ 已实现（同步微任务模型） |
| `new.target` | ✗ | 未实现 |
| 生成器 / 迭代器 / `yield` | ✗（`yield` 代码生成报错） | 未实现 |
| 闭包 `arity` / 函数属性 | — | `xt_closure_arity` 恒为 -1，未填充 |
| 函数对象属性（`fn.name` / `fn.length` / `fn.call` / `fn.apply` / `bind`） | ✗ | 未实现 |
| 调用参数展开 `f(...args)` | ✗ | 未实现 |
| 解构参数 / 解构绑定 | ✗ | 未实现 |

---

## 6. 类与面向对象（部分实现）

| 特性 | 现状 |
| --- | --- |
| `class` 声明 / 类表达式 | ✓ 已实现（原型对象 + 构造器闭包，存于 LLVM 全局） |
| 构造函数 `constructor` | ✓ |
| 实例字段 / 属性声明 | ✓（含 `this` 初始化器，在构造函数体前执行） |
| 方法 | ✓ |
| `static` 字段 / 方法 | ✓（存于构造器函数的属性包） |
| 继承 `extends` / `super` | ✓（单级正确；`super` 取 `this` 原型的原型，理论缺陷见第 8 节） |
| 原型链 / 方法查找 | ✓ |
| `instanceof` | ✓ |
| `new` / 实例化 | ✓ |
| `get` / `set` 访问器 | ✗ 仅解析，无访问器语义 |
| 参数属性 `constructor(public x: T)` | ✗ 不自动赋值 `this.x` |
| `private` / `protected` / `public` / `readonly` 修饰符 | ✗ 仅解析，无访问控制 |
| `abstract` / `implements` | ✗ |
| 私有字段 `#x` | ✗ |
| `enum` | ✗ |

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
| `finally` 与提前退出 | `return` / `break` / `continue` 离开 `try` 区域时**不会执行 `finally`**；仅正常完成、`catch` 完成后、以及未捕获异常传播时会执行 `finally`。运行时会正确弹出 `try` 帧，不会导致崩溃 |
| 异常对象 | 抛出 / 捕获的是任意值（字符串、数字、对象均可），且支持 `new Error(...)` / `extends Error`；但未捕获 `stack`，也未实现其它内置错误子类（`TypeError` 等） |
| `for...in` | 对对象 / 数组 / 字符串枚举键（数组与字符串得到字符串下标），但不含原型链属性，`delete` 后行为与 JS 基本一致 |
| 字符串 `length` | 运行时按 UTF-8 字节 / 码点计数，而非 JS 的 UTF-16 码元长度（emoji、非 BMP 字符长度会偏小） |
| 数字转字符串 | 仅覆盖常见情况（整数、最短往返），边界格式（科学计数法细节等）与 JS 不一致 |
| 宽松相等 `==` | 仅实现子集（number/string/bool/null/undefined），对象参与时按引用比较，未做 ToPrimitive |
| `+` 加法 | 数字 + 对象 / 数组等 ToPrimitive 路径不完整 |
| 对象展开 `{...obj}` | 仅复制对象自身可枚举属性；对数组 / 字符串展开的索引复制有限 |
| 可迭代对象展开 | 支持 `[...arr]`、`[...set]`、`[...map]`、`[...str]` 以及数组字面量展开（`["a", ...set]`）；调用参数展开 `f(...args)` 仍不支持 |
| 可选链 `?.` | 采用逐节点空值短路：`a?.b`、`a?.[b]`、`a?.b()`、`a?.[b]()`、`a?.()` 均正确短路；但链末再接非可选成员再调用的形式（如 `a?.b.c()`）不会整体短路，`a?.b.c` 会先得到 `undefined` 再对其取 `.c`，最终调用会抛错 |
| 数组越界 / 稀疏 | 越界访问返回 `undefined`；对 `arr.length` 赋值会截断 / 扩展，但不区分稀疏空洞 |
| 内存管理 | bump arena 永不释放，无 GC；长生命周期程序内存持续增长 |
| 函数 `arity` / 调用参数个数 | 无参数个数校验 |
| `async` / `await` | **同步微任务模型**：`await` 在已 settle 的 promise 上同步继续，pending promise 通过运行时微任务队列在 `await` 与程序结束时驱动；无真正的事件循环，无法等待定时器 / I/O |
| `super` | `super.x` / `super(...)` 取 `this` 原型的原型；单级继承正确，继承深度 > 1 时可能不准确 |
| 类 | 无访问控制；已实现 `get`/`set` 访问器与构造器参数属性 |
| `import` / `export` | 驱动层 AST 打包、顶层符号按模块前缀重命名；扩展模块可按裸/`node:` 说明符导入；相对模块的命名空间导入 `import * as` 未实现，循环依赖报错，`export *` 为近似 |

---

## 9. 工具链 / 平台 / 工程未实现

| 项 | 现状 |
| --- | --- |
| GC（垃圾回收） | ✗ 有意推迟；`xt_alloc` 已隔离，但尚未替换为精确 / 保守回收器 |
| 自举（self-hosting） | ✓ 编译器已能自编译：`xbintsc build src/cli/main.ts` 可产出可用二进制，且从第 1 代起产出的 IR 保持稳定。运行时仍为 C |
| 类型检查器 | ✗ 仅定义诊断码，无 checker |
| 完整标准库（Math / JSON / Date / 集合等） | 部分：Math / JSON / Date / Map / Set / RegExp / `Error` / `BigInt` 已实现；Symbol 未实现 |
| 多文件模块打包 | 部分：相对路径 `.ts` 导入打包已实现，另支持裸说明符的扩展模块导入；相对模块的命名空间导入 / 循环依赖 / npm 未实现 |
| 真正的异步运行时 / 事件循环 | ✗（Promise 为同步微任务模型） |
| Windows 二进制产物验证 | 构建层已适配（`.exe` 后缀、链接参数分支），但需 CI 验证（`.github/workflows` 已配置） |
| 精确的 ECMAScript 数值 / 字符串 / 比较语义 | ✗ 见第 8 节 |

---

## 10. 速查：未实现 / 部分实现清单

```
未实现（语句）：namespace/module、标签语句 label:

未实现（表达式）：标记模板、yield、调用参数展开 f(...args)

未实现（函数）：生成器、fn.name/length/call/apply/bind、new.target

未实现（类/面向对象）：访问控制、私有字段 #x

未实现（标准库）：Symbol、迭代器协议（自定义 Symbol.iterator）

未实现（模块）：相对模块的命名空间导入 import * as、循环依赖、npm 依赖

未实现（类型系统）：类型检查、泛型实例化、断言语义、可选链类型窄化

未实现（运行时）：GC、真正的异步事件循环、finally 的提前退出执行、
                 UTF-16 length、ToPrimitive 完整路径

未实现（工程）：GC 替换、类型检查器
```
