# 语义检查

> 属于[已实现的语法与功能](../implemented.md) · [English](../../implemented/checker.md)

位置：`src/checker/`（`checker.ts`、`undefined.ts`、`declarations.ts`、
`calls.ts`、`type-system.ts`、`assignability.ts`、`flow.ts`、`typecheck.ts`），
由 `generate()` 在绑定之后、生成 LLVM IR 之前调用。

检查器**默认开启**；`--no-check`（或编程接口里的 `check: false`）可以关闭。
它把无需类型推断的检查与一个小型结构化类型模型结合起来：

| 诊断 | 错误码 | 检查内容 |
| --- | --- | --- |
| `Cannot find name` | TS3002 | 标识符既没有声明，也不是运行时全局 |
| `Duplicate identifier` | TS3001 | 同作用域内不能合并的重复声明（两次 `let x`、`function f` 后又 `const f`、`let` 与参数同名……） |
| `Argument count mismatch` | TS4004 | 直接调用本地可见函数或 `const` 箭头/函数表达式时，实参过少或过多 |
| `Property not found` | TS4003 | 在可静态确定的形状上读取不存在的属性 |
| `Type mismatch` | TS4001 | 初始化、赋值、返回值或调用实参确定不可赋值 |
| `Not callable` | TS4002 | 调用的值类型是原始类型（number、string、boolean……） |

## 类型模型知道什么

检查器解析它能理解的类型语法，其余全部当作 `any`：

- 关键字类型、字面量类型、`A | B` 联合与对象类型字面量；
- 接口与类型别名（含泛型别名与 `extends`）；
- 类：实例字段、方法、访问器、参数属性、`this.x = ...` 成员与本地祖先类。
  静态成员不算实例成员；
- 数组、元组、函数类型、对象字面量与 `new LocalClass()`；
- 简单泛型：声明处的类型参数视为 `any`；调用时从实参推断（`T`、`T[]`、
  `Array<T>`），或使用显式类型实参。

模型叫不出名字的语法——映射/条件类型、`keyof`、`typeof` 查询、索引访问、
工具类型、被打包重命名后无法解析的导入类型、未知基类——都变成 `any`。
`any` 与一切兼容，这是核心安全属性：不支持的语法最多隐藏一个 bug，
绝不会拒绝能正常工作的代码。

## 可赋值性

在变量初始化、`=` 赋值、`return` 与调用实参处检查：

- 原始类型与字面量类型；
- 对象/接口/类形状：目标中每个必需属性都必须存在且可赋值；源可以有多余属性；
- 数组与元组逐元素检查；
- 函数：参数个数与返回类型（保守比较）；
- 联合：源的每个成员都必须能匹配目标成员，或目标某个成员能接受源。

`let`/`var` 推断加宽后的类型（`let x = 1` 是 `number`），`const` 保留字面量。
`null` 与 `undefined` 在任何需要值的位置都被接受（没有 strict-null 模式）。
函数返回类型不做推断：未标注返回类型的函数返回 `any`，因此调用方永远不会被误报。

## 属性是否存在

形状来自显式类型注解，或来自用对象字面量 / `new LocalClass(...)` 初始化的
`const`。可选属性、`Object.prototype` 成员（`toString`、`hasOwnProperty`……）、
索引签名与继承的类成员都会被接受。

当形状在声明后可能改变时，该变量会被放过：属性写/删/自增、可能修改它的调用
（`mutate(object)`、`Object.assign(object, ...)`）、别名（`const p = object`）、
动态构造函数（`Object.assign(this, ...)`、`this[key] = ...`）或原型扩展
（`Object.assign(C.prototype, ...)`）。给不存在的属性赋值是允许的
（xbintsc 程序经常在字面量之后扩展对象）；这次写入会把形状打开，而不是报错。

不检查：`object["x"]` 下标访问、解构、经函数返回值得到的形状、
经数组/对象容器产生的别名。

## 类型收窄

在分支内部，当条件是以下形式时检查器会收窄变量：

- `value !== null` / `!== undefined` / `!= null`（以及 `===` 形式）；
- `typeof value === "string" | "number" | "boolean" | "bigint" | "symbol" | "undefined"`；
- `value instanceof LocalClass`；
- 真值判断（`if (value)`、`value &&`、`value ||`）；
- 通过 `&&`、`||`、`??`、`!` 组合的形式。

`if`/`else` 与条件表达式（`cond ? a : b`）会应用收窄；其他控制流不会。

## 刻意不检查什么

- 完整推断：未标注的函数返回类型、复杂泛型约束与高阶推断都按 `any` 处理。
- 新对象字面量的多余属性检查。
- `as`/`satisfies` 断言可自由收窄或放宽（它们会被擦除）。
- 重载解析、映射类型、条件类型与模板字面量类型。
- `node_modules` 下被打包的源码完全不检查：检查器是针对你写的代码的门禁，
  因为依赖内部而失败会让默认开启的检查器无法与 npm 包共存。

## 关闭检查后的行为

`--no-check` 恢复旧的宽松流水线：类型语法被擦除后继续生成代码。它也会恢复
静默的 `Duplicate identifier` 行为（`let value = 1; let value = 2;` 可以编译，
第一个值生效）。建议保持检查开启。

## 缓存与诊断

`check` 是增量缓存 key 的一部分，因此 `--no-check` 的构建产物不会与已检查的
产物互相复用。检查开启时，代码生成不再重复报告自己的 `Cannot find name`，
每个名字只报一次。`xbintsc check` 会运行检查并在链接前停止，是编辑器和
pre-commit 钩子的快速门禁。
