# `buffer`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/buffer.md)

Location: `src/extensions/node/buffer/index.ts`, `runtime/ext_node/buffer/buffer.c`

xbintsc has no native binary value type, so a `Buffer` is an ordinary object
that keeps its bytes in an `ArrayBuffer` under a private symbol, plus a
`length` property, sharing the `xt_buffer_proto()` prototype that provides
instance methods. Dynamic-property hooks answer `buf[i]` and `buf[i] = 65` from
that store, so index access behaves like a real Buffer while the bytes stay out
of the property table: building one is a single bulk copy instead of one
property insert per byte (which was quadratic and stalled on multi-kilobyte
decodes). `xt_node_is_buffer` / `xt_node_buffer_bytes` let other modules access
bytes across modules; `xt_node_buffer_bytes` copies through
`xt_array_buffer_copy_bytes_out`, so the fs module does not pay one lookup per
byte.

| Static method | Notes |
| --- | --- |
| `Buffer.from(value[, encoding])` | construct from a string (hex / base64 / utf8), array or Buffer |
| `Buffer.alloc(size[, fill])` | allocate and fill |
| `Buffer.allocUnsafe(size)` | allocate |
| `Buffer.isBuffer(value)` | test |
| `Buffer.isUtf8(value)` | whether the bytes are valid UTF-8 |
| `Buffer.byteLength(value[, encoding])` | byte length |
| `Buffer.concat(list[, totalLength])` | concatenate |
| `Buffer.compare(a, b)` | compare |
| `buffer.constants` | `MAX_LENGTH` / `MAX_STRING_LENGTH` |

Instance methods: `toString([encoding])`, `toJSON()`, `slice(start, end)`, `subarray(...)`, `equals(other)`, `compare(other)`, `copy(target[, targetStart, sourceStart, sourceEnd])`, `write(string[, offset[, length[, encoding]]])`, `fill(value)`, `reverse()`, `indexOf(value)`, `lastIndexOf(value)`, `includes(value)`, `keys()`, `values()`, plus `readUInt8/UInt16LE/UInt16BE/UInt32LE/UInt32BE`, `writeUInt8/UInt16LE/UInt16BE/UInt32LE/UInt32BE`.

Index access works through the object hooks: `buf[0]` reads a byte, `buf[0] =
65` writes one (out-of-range writes are dropped and cannot grow the buffer),
`0 in buf` is true inside the range, and `Object.keys(buf)` lists the indices
after the real properties. The store is an `ArrayBuffer`, so each byte costs
one `xt_value` slot (the representation `ArrayBuffer` already uses) in exchange
for garbage collection without a finalizer.

```ts
const buf = Buffer.from("hello");
console.log(buf.toString(), buf.length);      // hello 5
console.log(Buffer.alloc(4, 65).toString());  // AAAA
```
