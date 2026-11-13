# `stream/web`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/stream-web.md)

Location: `src/extensions/node/stream-web/index.ts`,
`runtime/ext_node/stream_web/stream_web.c`.

Node's `stream/web` subpath re-exports the WHATWG stream classes. The
implementations live in the core runtime (see [Web Streams](../streams.md)), so
a compiled program can use the same classes as browser globals or import them by
name:

```ts
import { ReadableStream, TransformStream } from "node:stream/web";
```

| Export | Notes |
| --- | --- |
| `ReadableStream`, `ReadableStreamDefaultReader`, `ReadableStreamDefaultController` | full implementation |
| `WritableStream`, `WritableStreamDefaultWriter`, `WritableStreamDefaultController` | full implementation |
| `TransformStream`, `TransformStreamDefaultController` | full implementation |
| `TextEncoderStream`, `TextDecoderStream` | UTF-8 text transform streams |
| `CountQueuingStrategy`, `ByteLengthQueuingStrategy` | queuing strategies |
| `ReadableStreamBYOBReader`, `ReadableStreamBYOBRequest` | exported; constructors throw (no byte streams) |
| `CompressionStream`, `DecompressionStream` | exported; constructors throw (no compression streams) |
| `ReadableStreamTee` | `undefined`, so undici's `ReadableStreamTee?.(...) ?? body.stream.tee()` falls back to the body's own `tee` |

The constructors are first-class values: `const RS = ReadableStream`,
`new RS(...)`, `stream instanceof ReadableStream` and `ReadableStream.from(...)`
all work. See [Web Streams](../streams.md) for the async model, the supported
methods and the remaining limits (BYOB and compression streams).
