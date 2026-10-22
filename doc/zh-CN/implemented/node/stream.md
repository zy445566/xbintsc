# `stream` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/stream.md)

实现位置：`src/extensions/node/stream/index.ts`、`runtime/ext_node/stream/stream.c`

`Readable` / `Writable` / `Duplex` / `Transform` / `PassThrough` 作为**全局构造函数**使用（`new Readable()` 等）；`stream.Readable.from(...)` 等静态方法通过 `stream` 命名空间解析。流是 EventEmitter，采用**同步事件模型**：`on('data')` 时冲刷 `push` 缓冲，`write` 即时投递。

| 方法 | 说明 |
| --- | --- |
| `push(chunk)` / `read([n])` | Readable 端 |
| `write(chunk)` / `end([chunk])` | Writable 端 |
| `pipe(destination)` | 数据转发 |
| `on('data' / 'end' / 'finish')` | 事件 |
| `pause()` / `resume()` / `setEncoding(enc)` / `destroy()` | 流控制 |
