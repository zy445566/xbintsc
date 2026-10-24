# 类型系统

> 属于[未实现语法与功能](../unimplemented.md) · [English](../../unimplemented/types.md)

类型语法不再被盲目擦除：默认开启的[语义检查](../implemented/checker.md)
会解析它能建模的那部分 TypeScript 类型，并检查可赋值性、属性是否存在与
简单的控制流收窄。类型语法在代码生成阶段仍然被擦除，检查器是刻意不完整的：

- **没有完整推断。** 未标注返回类型的函数返回 `any`；泛型约束、高阶推断与
  返回类型推断都不做。
- **没有 strict-null 模式。** `null` / `undefined` 在任何需要值的位置都可赋值。
- **没有新对象字面量的多余属性检查。**
- **没有重载解析、映射类型、条件类型与模板字面量类型。** 它们解析为 `any`。
- **`as` / `satisfies` / 非空 `!`** 仍被擦除，不做任何校验；检查器把它们当作断言。
- **复杂泛型。** 简单类型参数可从实参推断（`T`、`T[]`、`Array<T>`）；
  映射/条件类型那套泛型机制不做。
- **类型实参个数不检查**；未知类型参数就是 `any`。

检查器的六个诊断码现在都会使用：`DuplicateIdentifier`（TS3001）、
`CannotFindName`（TS3002）、`TypeMismatch`（TS4001）、`NotCallable`（TS4002）、
`PropertyNotFound`（TS4003）与 `ArgumentCountMismatch`（TS4004）。
