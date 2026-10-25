# 语法分析（Parser）

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/parser.md)

实现位置：`src/parser/parser.ts`、`src/ast/nodes.ts`

模块语法（`import` / `export`、import attributes）及其驱动层打包见[模块系统](./modules.md)。

## 语句

- 变量声明：`var` / `let` / `const`，支持多声明符 `const a = 1, b = 2;`
- 函数声明、函数表达式和方法，含 `async` 修饰、生成器（`function*`、`yield`、`yield*`、`.next`/`.throw`/`.return`）与异步生成器（`async function*`、`for await...of`）
- `class` 声明 / 类表达式（构造函数、字段、方法、`static`、`static { ... }` 静态块、`extends`）
- `if` / `else`
- `while`、`do...while`
- `for`（初始化、条件、增量均可省略）
- `for...of`、`for...in`
- `return`、`break`、`continue`、`throw`（含带标签的 `break label` / `continue label`）
- `switch` / `case` / `default`（含穿透 fall-through）
- `try` / `catch` / `finally`（基于运行时 setjmp 帧的可捕获异常）
- `export var` / `export let` / `export const`（修饰符解析后擦除）
- 块语句 `{}`、空语句 `;`、`debugger;`
- 表达式语句

## 表达式

- 全部常见运算符及优先级 / 结合性（见[运算符](./operators.md)）
- 赋值表达式与全部复合赋值（见[运算符](./operators.md)）
- 条件（三元）表达式 `a ? b : c`
- 逗号（序列）表达式 `a, b, c`（可用于 `for` 头、括号组与表达式语句）
- 箭头函数 `() => expr` / `() => { ... }`（含类型参数、返回类型注解）
- 函数表达式 `function () {}` 与命名函数表达式 `function g() {}`（也支持 `async function` 表达式）
- 调用表达式 `f(...)`、成员访问 `a.b`、元素访问 `a[i]`
- 数组字面量 `[1, 2]`、稀疏数组 elision、展开 `[...a]`（也支持字符串、`Map`、`Set`）
- 对象字面量 `{ a: 1 }`、简写属性 `{ a }`、方法简写 `{ m() {} }`、getter/setter 简写 `{ get x() {} }` / `{ set x(v) {} }`、计算属性名 `{ [expr]: 1 }`、对象展开 `{ ...obj }`
- 模板字面量 `${}` 替换、标记模板（含 raw 字符串与 `String.raw`）
- 括号表达式、`as` / `satisfies` / 非空断言 `!`（类型擦除）
- 一元：`+ - ! ~ typeof void`、前缀 / 后缀 `++ --`
- 可选链 `?.` / `?.[]` / `?.()`（含空值短路语义）
- `delete` 表达式（删除对象属性）

## TypeScript 类型语法（仅解析、结构保留后擦除）

- 类型注解、返回类型注解、类型参数 `<T>` 与约束 `<T extends U>`、类型参数默认值
- 类型引用、限定名 `A.B`
- 联合 `|`、交叉 `&`、数组 `T[]`、元组 `[T, U]`、可选元组成员 `T?`、剩余元组成员 `...T`
- 函数类型 `(a: T) => U`、构造签名 `new () => T`
- 对象类型字面量、属性签名、方法签名、索引签名 `[k: string]: T`
- 条件类型 `T extends U ? X : Y`、映射类型 `{ [K in T]: U }`、`infer`
- 类型运算符 `keyof`、`unique`、`readonly`
- `typeof`（类型查询）、索引访问类型 `T[K]`
- 字面量类型、`this` 类型
- 类型谓词 `x is T` / `asserts x is T`
- 接口、类型别名、枚举、命名空间 / 模块声明
- `import` / `export` 的各种形式（结构解析）

## ASI

- 自动分号插入（Automatic Semicolon Insertion），依据 `precededByLineBreak` / `}` / EOF。
