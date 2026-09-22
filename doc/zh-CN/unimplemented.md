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

| 语法 | 现状 | 说明 |
| --- | --- | --- |
| `yield` 表达式（生成器） | 解析 ✓，代码生成 ✗ | → "does not yet support this expression (yield expression)" |

> 标记模板已实现，含 raw 字符串与 `String.raw`。`import.meta` 可解析，但无运行时取值。

### 2.2 解析器不支持

目前未知有表达式级别的缺口。

### 2.3 运算符未实现

| 运算符 | 现状 | 说明 |
| --- | --- | --- |
| `new.target` | ✗ | 未实现 |
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
- `Error`（`new Error(...)`、`extends Error`）

### 3.3 仍未实现

| 类别 | 现状 |
| --- | --- |
| `Symbol` 构造器与 symbol 原始值 | ✗ 未实现 |
| `String.prototype.normalize` | ✗ 未实现 |
| `structuredClone` | ✗ 未实现 |
| 内置错误子类（`TypeError`、`RangeError` 等） | ✗ 仅 `Error`；运行时内部抛出使用它 |
| `AggregateError` 构造器 | ✗ `Promise.any` 已实现，但 rejection 为字符串而非 `AggregateError` 对象 |
| 迭代器协议 / `Symbol.iterator` / `for...of` 自定义可迭代 | 部分：数组、字符串、`Map`、`Set` 均可在 `for...of` / 展开中使用；不读取自定义 `Symbol.iterator` |
| 生成器 / 异步迭代 | ✗ 未实现 |
| 定时器 / I/O / 进程等宿主 API | 仅通过扩展（如 Node `fs`）提供 |

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
| 第三方 / npm 依赖 | ✗ 未实现（仅相对路径 `.ts` 文件） |

> 扩展模块（如 `fs`）可通过裸名称或 `node:` 前缀的 `import` 引入 —— `import { readFileSync } from "fs"` / `import path from "path"` —— 并解析到运行时入口（具名、默认与命名空间形式均可）。相对模块仍在驱动层打包。

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
| `async` / `await` / Promise | ✓ | ✓ 已实现（同步微任务模型） |
| `new.target` | ✗ | 未实现 |
| 生成器 / 迭代器 / `yield` | ✗（`yield` 代码生成报错） | 未实现 |
| 闭包 `arity` | — | `xt_closure_arity` 恒为 -1，未填充 |
| `fn.call` / `fn.apply` / `fn.bind` | ✓ | ✓ 已实现（绑定闭包不跟踪部分参数的 `length`） |
| `fn.name` / `fn.length` | ✗ | 未实现 |
| 内置方法一等公民（`typeof arr.map`、`const f = arr.push`、`obj.method?.()`） | ✗ | 内置方法只能通过直接调用（`arr.map(...)`）访问；作为值读取会得到 `undefined` |

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
| 字符串 `length` | 按 UTF-8 字节计数，而非 UTF-16 码元（`"\u00e9".length` 报 1 而非 2；`"\u{1F600}".length` 报 4 而非 2；`codePointAt` 同样有偏差） |
| `Object.getPrototypeOf({})` | 返回 `undefined`，而非 `Object.prototype` 对象 |
| 全局正则 `lastIndex` | `/g`、`/y` 正则的 `test` / `exec` 不推进也不读取调用方设置的 `lastIndex` |
| `String.normalize` / `structuredClone` / `Symbol` | 未实现（见第 3 节） |
| 内置方法一等公民 | `typeof arr.map` 为 `"undefined"`；脱离接收者的内置方法无法调用，`obj.method?.()` 也未绑定 `this` |
| `for...in` | 枚举对象 / 数组 / 字符串的自身键；不含原型链属性 |
| 数组越界 / 稀疏 | 越界访问返回 `undefined`；对 `arr.length` 赋值会截断 / 扩展，但不区分稀疏空洞 |
| 内存管理 | bump arena 永不释放，无 GC；长生命周期程序内存持续增长 |
| 函数 `arity` / 调用参数个数 | 无参数个数校验；`fn.length` 不可用 |
| `async` / `await` | **同步微任务模型**：`await` 在已 settle 的 promise 上同步继续；无真正的事件循环，无法等待定时器 / I/O |
| `super` | `super.x` / `super(...)` 取 `this` 原型的原型；单级继承正确，继承深度 > 1 时可能不准确 |
| `Error.stack` | 未捕获 |
| 模块实时绑定 | 命名空间导入与导入绑定为快照（见第 4 节） |
| `import.meta` | 可解析但无取值 |

> 数字格式化、宽松相等、`+` / 关系运算 `ToPrimitive`、整链可选链、`finally` 提前退出、对象键顺序等均已对齐 Node，并由差分测试覆盖，故不再列为偏差。

---

## 9. 工具链 / 平台 / 工程未实现

| 项 | 现状 |
| --- | --- |
| GC（垃圾回收） | ✗ 有意推迟；`xt_alloc` 已隔离，但尚未替换为精确 / 保守回收器 |
| 自举（self-hosting） | ✓ 编译器已能自编译：`xbintsc build src/cli/main.ts` 可产出可用二进制，且从第 1 代起产出的 IR 保持稳定。运行时仍为 C |
| 类型检查器 | ✗ 仅定义诊断码，无 checker |
| 完整标准库 | 部分：Math / JSON / Date / Map / Set / RegExp / `Error` / `BigInt` 已实现；Symbol 未实现 |
| 多文件模块打包 | 部分：相对路径 `.ts` 打包、命名空间导入、裸说明符扩展模块导入已实现；循环依赖 / npm / 实时绑定未实现 |
| 真正的异步运行时 / 事件循环 | ✗（Promise 为同步微任务模型） |
| Windows 二进制产物验证 | 构建层已适配（`.exe` 后缀、链接参数分支），并在 CI 中验证 |
| 精确的 ECMAScript 数值 / 字符串 / 比较语义 | 部分，见第 8 节 |

---

## 10. 速查：未实现 / 部分实现清单

```
未实现（语句）：namespace/module 声明

未实现（表达式）：yield（生成器）、new.target、import.meta 取值

未实现（函数）：生成器、fn.name/length、内置方法一等公民

未实现（类/面向对象）：abstract/implements、访问控制、父子类 #x 同名

未实现（标准库）：Symbol、String.normalize、structuredClone、
                  TypeError/RangeError 子类、AggregateError、
                  迭代器协议（Symbol.iterator）

未实现（模块）：循环依赖、npm 依赖、实时绑定

未实现（类型系统）：类型检查、泛型实例化、断言语义、可选链类型窄化

未实现（运行时）：GC、真正的异步事件循环、UTF-16 length、
                  Object.prototype 身份、全局正则 lastIndex

未实现（工程）：GC 替换、类型检查器
```
