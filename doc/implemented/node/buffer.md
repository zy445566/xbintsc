# `buffer`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/buffer.md)

Location: `src/extensions/node/buffer/index.ts`, `runtime/ext_node/buffer/buffer.c`

xbintsc has no native binary value type, so `Buffer` is represented as a
**plain object**: each byte is a numeric property `"0".."n-1"`, plus a `length`
property, sharing the `xt_buffer_proto()` prototype that provides instance
methods. `xt_node_is_buffer` / `xt_node_buffer_bytes` let other modules access
bytes across modules.

| Static method | Notes |
| --- | --- |
| `Buffer.from(value[, encoding])` | construct from a string (hex / base64 / utf8), array or Buffer |
| `Buffer.alloc(size[, fill])` | allocate and fill |
| `Buffer.allocUnsafe(size)` | allocate |
| `Buffer.isBuffer(value)` | test |
| `Buffer.byteLength(value[, encoding])` | byte length |
| `Buffer.concat(list[, totalLength])` | concatenate |
| `Buffer.compare(a, b)` | compare |

Instance methods: `toString([encoding])`, `toJSON()`, `slice(start, end)`, `subarray(...)`, `equals(other)`, `compare(other)`, `copy(target[, targetStart, sourceStart, sourceEnd])`, `write(string[, offset[, length[, encoding]]])`, `fill(value)`, `reverse()`, `indexOf(value)`, `lastIndexOf(value)`, `includes(value)`, `keys()`, `values()`, plus `readUInt8/UInt16LE/UInt16BE/UInt32LE/UInt32BE`, `writeUInt8/UInt16LE/UInt16BE/UInt32LE/UInt32BE`.

```ts
const buf = Buffer.from("hello");
console.log(buf.toString(), buf.length);      // hello 5
console.log(Buffer.alloc(4, 65).toString());  // AAAA
```
