# `zlib`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/zlib.md)

Location: `src/extensions/node/zlib/index.ts`, `runtime/ext_node/zlib/zlib.c`

- `createGzip()` returns a gzip transform consumed by
  [`stream/promises`](./stream-promises.md) `pipeline`.
- `crc32(data[, value])` computes the CRC-32 checksum.
- `constants` exposes the zlib flag/error values, and `Z_DEFAULT_WINDOWBITS`
  the default window size (15).
