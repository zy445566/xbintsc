# `crypto` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/crypto.md)

实现位置：`src/extensions/node/crypto/index.ts`、`runtime/ext_node/crypto/crypto.c`

- `createHash(algorithm)` 返回带 `update(...)` 与 `digest([encoding])` 的哈希对象。
- 支持流式形式（`setEncoding` / `write` / `end` / `read`）。
- 提供 SHA-1 与 SHA-256。
