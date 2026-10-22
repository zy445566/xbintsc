# `stream/promises` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/stream-promises.md)

实现位置：`src/extensions/node/stream-promises/index.ts`、`runtime/ext_node/stream/pipeline.c`

- `pipeline(...)` 同步排空给定流，并返回一个已 resolve 的 Promise，因此 `await pipeline(...)` 可用。
- 支持的转换阶段为 `zlib.createGzip()`，见 [`zlib`](./zlib.md)。
