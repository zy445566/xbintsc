# 类型系统

> 属于[未实现语法与功能](../unimplemented.md) · [English](../../unimplemented/types.md)

类型语法被解析为 AST 后在绑定 / 代码生成阶段**擦除**。这里没有可赋值性检查：
类型注解只是文档，不是约束。默认开启的[语义检查](../implemented/checker.md)
只覆盖无需类型推断的事实：

- 类型注解、返回类型、类型参数、类型别名、接口、泛型约束等：解析 ✓，类型检查 ✗。
- 实际会发出的检查器诊断：`DuplicateIdentifier`（TS3001）、
  `CannotFindName`（TS3002）与 `ArgumentCountMismatch`（TS4004）。
  `TypeMismatch`（TS4001）、`NotCallable`（TS4002）与 `PropertyNotFound`
  （TS4003）已定义但**未使用**。
- `as` / `satisfies` / 非空断言 `!`：直接擦除，不做任何断言语义。
- 泛型：无运行时实例化，类型参数被忽略。
- 可选链 `?.` 的类型窄化：无（运行时短路已实现，但无类型层面的窄化）。
