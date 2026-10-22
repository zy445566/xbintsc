# 字符串与文本

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/strings.md)

实现位置：字符串值在核心运行时（`runtime/xt_values.c`）；方法表面由
`runtime/xt_stdlib.c` / `xt_stdlib2.c` 分发；`TextEncoder` / `TextDecoder` 位于
`runtime/xt_text_codec.c`。

## 值模型

- UTF-8 存储，对外呈现 JavaScript 的 UTF-16 视图：`length`、索引、`slice`、`indexOf`、`split`、`padStart`、`codePointAt` 按码元计数；`for...of` 与 `Array.from` 按码点计数；孤立代理项以 WTF-8 保存，并在拼接时重新组合。
- 惰性 cons 节点拼接：首次观察时才扁平化，因此循环追加保持线性复杂度。
- 相等比较、格式化数字转字符串。
- `xt_string_length` 供 I/O 使用，返回字节数；`xt_string_utf16_length` 返回 JavaScript 长度。

## 方法

- `charAt` `charCodeAt` `codePointAt` `indexOf` `lastIndexOf` `includes` `startsWith` `endsWith` `slice` `substring` `substr` `split` `match` `replace` `replaceAll` `search` `toUpperCase` `toLowerCase` `trim` `trimStart` `trimEnd` `padStart` `padEnd` `repeat` `concat` `at` `localeCompare` `valueOf`
- `String.match` / `lastIndexOf` / 带 limit 的 `split`；正则捕获组可经 `match` / `replace` / `split` / `search` 使用（见[标准库](./stdlib.md)）。
- `String` 静态：`fromCharCode` / `fromCodePoint` / `raw`；标记模板保留 raw 字符串。

## 文本编码

- `TextEncoder` / `TextDecoder` 通过 `Uint8Array` 做编解码；仅支持 UTF-8。

已知偏差（`String.prototype.normalize`、正则按字节匹配）见
[未实现：标准库](../unimplemented/stdlib.md)。
