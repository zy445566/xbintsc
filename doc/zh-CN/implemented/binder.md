# 名字绑定与作用域（Binder）

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/binder.md)

实现位置：`src/binder/binder.ts`

- 作用域种类：模块、函数、块、`for`、`catch`
- 符号种类：`var` / `let` / `const` / `function` / `parameter` / `class` / `interface` / `type` / `enum` / `import` / `namespace`
- `var` 与函数声明提升到函数作用域；`let` / `const` 保持块作用域
- 标识符 → 声明 的解析；未解析标识符收集（`CannotFindName`）
- 闭包捕获分析：被内层函数引用的外部变量标记 `captured` / `boxed`，并穿过中间闭包传递捕获索引
- 参数登记为局部符号；命名函数表达式自名登记为 `const`
- 类型专用声明（interface / type alias）不参与值捕获
