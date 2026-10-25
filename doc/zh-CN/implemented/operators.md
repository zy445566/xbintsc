# 运算符与赋值

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/operators.md)

实现位置：`src/codegen/llvm.ts`（`BINARY_RUNTIME`、`emitPrefix`、`emitPostfix`、`emitAssignment`）

## 算术

`+ - * / % **`（`**` 右结合）

## 比较与相等

`< <= > >=`、`== !=`（宽松相等）、`=== !==`（严格相等）

## 逻辑与短路

`&& || ??`（含短路求值）、`!`

## 位运算

`& | ^ ~ << >> >>>`

## 一元

`+ - ! ~`、前缀 / 后缀 `++ --`

## 赋值

`= += -= *= /= %= **= <<= >>= >>>= &= |= ^= &&= ||= ??=`

## 其他表达式运算符

- 逗号表达式 `,`（降级为 `SequenceExpression`：从左到右求值每个操作数，结果为最后一个）
- `in` 运算符（`key in obj`，映射 `xt_in`）
- `delete obj.key` / `delete obj[key]`（映射 `xt_delete`）
- `instanceof`（映射 `xt_instance_of`，沿原型链判断）

> `typeof` / `void` 作为一元运算符已实现。
