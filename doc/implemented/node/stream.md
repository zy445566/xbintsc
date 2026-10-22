# `stream`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/stream.md)

Location: `src/extensions/node/stream/index.ts`, `runtime/ext_node/stream/stream.c`

`Readable` / `Writable` / `Duplex` / `Transform` / `PassThrough` are used as
**global constructors** (`new Readable()` etc.); statics such as
`stream.Readable.from(...)` are resolved through the `stream` namespace. Streams
are EventEmitters with a **synchronous event model**: `on('data')` flushes the
`push` buffer, and `write` delivers immediately.

| Method | Notes |
| --- | --- |
| `push(chunk)` / `read([n])` | Readable side |
| `write(chunk)` / `end([chunk])` | Writable side |
| `pipe(destination)` | forward data |
| `on('data' / 'end' / 'finish')` | events |
| `pause()` / `resume()` / `setEncoding(enc)` / `destroy()` | flow control |
