# `zlib` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/zlib.md)

实现位置：`src/extensions/node/zlib/index.ts`、`runtime/ext_node/zlib/zlib.c`

- 仅实现 `createGzip()`。它返回 gzip 转换流，供
  [`stream/promises`](./stream-promises.md) 的 `pipeline` 使用。
