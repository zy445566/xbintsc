# `zlib`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/zlib.md)

Location: `src/extensions/node/zlib/index.ts`, `runtime/ext_node/zlib/zlib.c`

- Only `createGzip()` is implemented. It returns a gzip transform consumed by
  [`stream/promises`](./stream-promises.md) `pipeline`.
