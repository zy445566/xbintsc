# `crypto`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/crypto.md)

Location: `src/extensions/node/crypto/index.ts`, `runtime/ext_node/crypto/crypto.c`

- `createHash(algorithm)` returns a hash object with `update(...)` and `digest([encoding])`.
- The streaming form (`setEncoding` / `write` / `end` / `read`) is supported.
- SHA-1 and SHA-256 are available.
- `randomBytes(size[, callback])`, `randomUUID()`, `randomInt([min, ]max[, callback])`,
  `randomFillSync(buffer[, offset[, size]])` and `randomFill(...)` read the OS
  CSPRNG (`BCryptGenRandom` on Windows, `/dev/urandom` elsewhere). `webcrypto`
  exposes the same `getRandomValues` / `randomUUID` object as the global
  `crypto`.
- `pbkdf2Sync(password, salt, iterations, keylen[, digest])` and
  `pbkdf2(...)` implement PBKDF2-HMAC-SHA1/SHA-256 (the async form invokes the
  callback immediately; the work is synchronous).
