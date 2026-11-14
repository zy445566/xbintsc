# `buffer` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/buffer.md)

实现位置：`src/extensions/node/buffer/index.ts`、`runtime/ext_node/buffer/buffer.c`

xbintsc 没有原生的二进制值类型，`Buffer` 是一个普通对象：字节存在私有 symbol 下的 `ArrayBuffer` 里，另有一个 `length` 属性，并共享 `xt_buffer_proto()` 原型提供实例方法。对象钩子（dynamic-property hooks）把 `buf[i]` 和 `buf[i] = 65` 接到这份存储上，所以索引访问的行为与真实 Buffer 一致，而字节不会进入属性表：构造整个 Buffer 是一次批量拷贝，而不是每字节插入一个属性（那种做法是 O(n²)，几 KB 的解码就会卡死）。`xt_node_is_buffer` / `xt_node_buffer_bytes` 供其它模块跨模块访问字节；`xt_node_buffer_bytes` 通过 `xt_array_buffer_copy_bytes_out` 拷贝，fs 模块因此不必每字节查一次属性。

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

索引访问走对象钩子：`buf[0]` 读一个字节，`buf[0] = 65` 写一个字节（越界写入被丢弃，不会让 Buffer 变长），`0 in buf` 在范围内为 true，`Object.keys(buf)` 会在真实属性之后列出这些下标。存储用的是 `ArrayBuffer`，因此每个字节占一个 `xt_value` 槽位（`ArrayBuffer` 本来就是这个表示），换来的是不需要终结器的垃圾回收。

```ts
const buf = Buffer.from("hello");
console.log(buf.toString(), buf.length);      // hello 5
console.log(Buffer.alloc(4, 65).toString());  // AAAA
```
