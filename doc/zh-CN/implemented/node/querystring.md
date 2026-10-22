# `querystring` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/querystring.md)

位置：`src/extensions/node/querystring/index.ts`、`runtime/ext_node/querystring/querystring.c`

| 函数 | 说明 |
| --- | --- |
| `parse(str[, sep[, eq]])` / `decode` | 解析为对象；重复的键会变成数组 |
| `stringify(obj[, sep[, eq]])` / `encode` | 序列化；空格变成 `+`，数组会重复键 |
| `escape(str)` / `unescape(str)` | 百分号编码 / 解码（`+` 解码为空格） |

默认值：`sep = "&"`，`eq = "="`。

```ts
import { parse, stringify } from "querystring";

const q = parse("a=1&b=2&b=3");      // { a: "1", b: ["2", "3"] }
console.log(stringify({ x: "a b" })); // x=a+b
```
