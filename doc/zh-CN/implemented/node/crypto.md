# `crypto` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/crypto.md)

实现位置：`src/extensions/node/crypto/index.ts`、`runtime/ext_node/crypto/crypto.c`

- `createHash(algorithm)` 返回带 `update(...)` 与 `digest([encoding])` 的哈希对象。
- 支持流式形式（`setEncoding` / `write` / `end` / `read`）。
- 提供 SHA-1 与 SHA-256。
- `randomBytes(size[, callback])`、`randomUUID()`、`randomInt([min, ]max[, callback])`、
  `randomFillSync(buffer[, offset[, size]])` 与 `randomFill(...)` 读取操作系统
  CSPRNG（Windows 上的 `BCryptGenRandom`，其它平台的 `/dev/urandom`）。
  `webcrypto` 暴露与全局 `crypto` 相同的 `getRandomValues` / `randomUUID` 对象。
- `pbkdf2Sync(password, salt, iterations, keylen[, digest])` 与 `pbkdf2(...)`
  实现 PBKDF2-HMAC-SHA1/SHA-256（异步形式会立即调用回调，计算本身是同步的）。
