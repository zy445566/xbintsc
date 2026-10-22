# 表达式与运算符

> 属于[未实现语法与功能](../unimplemented.md) · [English](../../unimplemented/expressions.md)

## 解析但代码生成报 `UnsupportedFeature`

无——生成器已实现。

> 标记模板已实现，含 raw 字符串与 `String.raw`。生成器（`function*`、`yield`、`yield*`）
> 基于有栈协程实现，支持 `next`/`throw`/`return`、`for...of`、展开与委托；
> `async function*` 与 `for await...of` 已实现：函数体可在 `await` 与 `yield` 两处挂起，
> 每次 `.next()` 返回 Promise。`import.meta` 提供 `url`、`dirname`、`filename`，
> 由正在运行的可执行文件路径解析（见[模块系统](./modules.md)）。

## 解析器不支持

目前未知有表达式级别的缺口。

## 运算符未实现

| 运算符 | 现状 | 说明 |
| --- | --- | --- |
| `new.target` | ✗ | 已解析，但以 `UnsupportedFeature` 明确拒绝（无 `new.target` 语义） |
| `typeof` / `void` / `in` / `delete` / `instanceof` | ✓ | 映射到运行时辅助函数（`xt_typeof`、`xt_in`、`xt_delete`、`xt_instance_of`） |
