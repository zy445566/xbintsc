# `crypto`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/crypto.md)

Location: `src/extensions/node/crypto/index.ts`, `runtime/ext_node/crypto/crypto.c`

- `createHash(algorithm)` returns a hash object with `update(...)` and `digest([encoding])`.
- The streaming form (`setEncoding` / `write` / `end` / `read`) is supported.
- SHA-1 and SHA-256 are available.
