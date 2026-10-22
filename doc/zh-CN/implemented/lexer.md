# 词法分析（Lexer）

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/lexer.md)

实现位置：`src/lexer/scanner.ts`、`src/lexer/token.ts`

- 完整 Token 分类：标识符、关键字、私有标识符、数字、字符串、模板、正则（扫描）、所有标点与运算符。
- 完整关键字表：`abstract any as asserts async await bigint boolean break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface is keyof let module namespace never new null number object package private protected public readonly return satisfies set static string super switch symbol this throw true try type typeof undefined unique unknown var void while with yield`
- 数字字面量：
  - 十进制、`0x` 十六进制、`0o` 八进制、`0b` 二进制
  - 下划线分隔 `1_000_000`
  - 小数、指数 `1.5e3`
  - BigInt 字面量（`10n`、`0xFFn`）求值为任意精度整数（算术、位运算、移位、比较、`toString(radix)`、`BigInt()` / `BigInt.asIntN` / `BigInt.asUintN`）
  - BigInt 字面量的值在词法 / 语法阶段即精确解析为 `bigint`（不经过 `Number`），超过 2^53 的字面量在 token / AST 中也保持完整精度
- 字符串字面量：
  - 单引号 / 双引号
  - 转义：`\n \t \r \b \f \0 \\ \' \"`、`\xHH`、`\uHHHH`、`\u{...}`
- 模板字面量（词法层）：无替换模板、模板头 `TemplateHead`、模板中 `TemplateMiddle`、模板尾 `TemplateTail`，支持 `${}` 与转义。
- 正则字面量扫描（`/pattern/flags`），并区分除法 `/`。
- 私有标识符 `#name`。
- 注释：单行 `//` 与块注释 `/* */`（含未闭合诊断）。
- 换行与空白跟踪（用于 ASI），BOM / CRLF 规范化（`SourceFile`）。
- 词法诊断：未终止字符串 / 模板 / 注释、非法字符、非法数字、非法转义。
