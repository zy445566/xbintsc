# `querystring`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/querystring.md)

Location: `src/extensions/node/querystring/index.ts`, `runtime/ext_node/querystring/querystring.c`

| Function | Notes |
| --- | --- |
| `parse(str[, sep[, eq]])` / `decode` | parse into an object; repeated keys become arrays |
| `stringify(obj[, sep[, eq]])` / `encode` | serialize; spaces become `+`, arrays repeat the key |
| `escape(str)` / `unescape(str)` | percent-encode / decode (`+` decodes to a space) |

Defaults: `sep = "&"`, `eq = "="`.

```ts
import { parse, stringify } from "querystring";

const q = parse("a=1&b=2&b=3");      // { a: "1", b: ["2", "3"] }
console.log(stringify({ x: "a b" })); // x=a+b
```
