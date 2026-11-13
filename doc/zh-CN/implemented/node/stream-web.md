# `stream/web`

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/stream-web.md)

实现位置：`src/extensions/node/stream-web/index.ts`、`runtime/ext_node/stream_web/stream_web.c`。

Node 的 `stream/web` 子路径重新导出 WHATWG 流类。实现位于核心运行时（见 [Web Streams](../streams.md)），因此编译后的程序既可以把它当作浏览器全局使用，也可以按名字导入：

```ts
import { ReadableStream, TransformStream } from "node:stream/web";
```

| 导出 | 说明 |
| --- | --- |
| `ReadableStream`、`ReadableStreamDefaultReader`、`ReadableStreamDefaultController` | 完整实现 |
| `WritableStream`、`WritableStreamDefaultWriter`、`WritableStreamDefaultController` | 完整实现 |
| `TransformStream`、`TransformStreamDefaultController` | 完整实现 |
| `TextEncoderStream`、`TextDecoderStream` | UTF-8 文本转换流 |
| `CountQueuingStrategy`、`ByteLengthQueuingStrategy` | 队列策略 |
| `ReadableStreamBYOBReader`、`ReadableStreamBYOBRequest` | 有导出；构造函数抛错（不支持字节流） |
| `CompressionStream`、`DecompressionStream` | 有导出；构造函数抛错（不支持压缩流） |
| `ReadableStreamTee` | 为 `undefined`，undici 的 `ReadableStreamTee?.(...) ?? body.stream.tee()` 会回退到 body 自身的 `tee` |

构造函数是一等值：`const RS = ReadableStream`、`new RS(...)`、`stream instanceof ReadableStream` 与 `ReadableStream.from(...)` 均可用。异步模型、支持的方法与剩余限制（BYOB、压缩流）见 [Web Streams](../streams.md)。
