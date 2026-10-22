# 语句

> 属于[未实现语法与功能](../unimplemented.md) · [English](../../unimplemented/statements.md)

## 解析但代码生成报 `UnsupportedFeature`

| 语法 | 现状 | 说明 |
| --- | --- | --- |
| `namespace` / `module` 声明 | 解析 ✓，代码生成 ✗ | → "does not yet support this statement (module declaration)" |

> 标签语句（`label: statement`、`break label`、`continue label`）已实现，包括非循环语句上的标签。

## 解析器不支持（直接语法错误）

目前未知有语句级别的缺口。解析器有意保持宽松，接受绝大多数 TypeScript 语句语法；不支持的形式会在代码生成阶段报错（见上）。
