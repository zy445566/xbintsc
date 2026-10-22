# `stream/promises`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/stream-promises.md)

Location: `src/extensions/node/stream-promises/index.ts`, `runtime/ext_node/stream/pipeline.c`

- `pipeline(...)` drains the given streams synchronously and returns an already-resolved promise, so `await pipeline(...)` works.
- `zlib.createGzip()` is the supported transform stage; see [`zlib`](./zlib.md).
