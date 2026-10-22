# `buffer` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/buffer.md)

实现位置：`src/extensions/node/buffer/index.ts`、`runtime/ext_node/buffer/buffer.c`

xbintsc 没有原生的二进制值类型，`Buffer` 以**普通对象**表示：每个字节是数字属性 `"0".."n-1"`，再加一个 `length` 属性，并共享 `xt_buffer_proto()` 原型提供实例方法。`xt_node_is_buffer` / `xt_node_buffer_bytes` 供其它模块跨模块访问字节。

| 静态方法 | 说明 |
| --- | --- |
| `Buffer.from(value[, encoding])` | 从字符串（hex / base64 / utf8）、数组或 Buffer 构造 |
| `Buffer.alloc(size[, fill])` | 分配并填充 |
| `Buffer.allocUnsafe(size)` | 分配 |
| `Buffer.isBuffer(value)` | 判定 |
| `Buffer.byteLength(value[, encoding])` | 字节长度 |
| `Buffer.concat(list[, totalLength])` | 拼接 |
| `Buffer.compare(a, b)` | 比较 |

实例方法：`toString([encoding])`、`toJSON()`、`slice(start, end)`、`subarray(...)`、`equals(other)`、`compare(other)`、`copy(target[, targetStart, sourceStart, sourceEnd])`、`write(string[, offset[, length[, encoding]]])`、`fill(value)`、`reverse()`、`indexOf(value)`、`lastIndexOf(value)`、`includes(value)`、`keys()`、`values()`，以及 `readUInt8/UInt16LE/UInt16BE/UInt32LE/UInt32BE`、`writeUInt8/UInt16LE/UInt16BE/UInt32LE/UInt32BE`。

```ts
const buf = Buffer.from("hello");
console.log(buf.toString(), buf.length);      // hello 5
console.log(Buffer.alloc(4, 65).toString());  // AAAA
```
