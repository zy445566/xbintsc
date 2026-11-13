# Web Streams

> 属于[已实现的语法与特性](../implemented.md) · [English](../../implemented/streams.md)

实现位置：`runtime/xt_streams.c` 与 `runtime/xt_streams/`。这些类是核心运行时全局对象，无需任何扩展开关即可使用；Node 的 `stream/web` 模块重新导出同一批构造函数（见 [stream/web](./node/stream-web.md)）。

## 类

| 类 | 说明 |
| --- | --- |
| `ReadableStream` | underlying source 的 `start` / `pull` / `cancel`；`getReader`、`cancel`、`tee`、`pipeTo`、`pipeThrough`、`values`、`[Symbol.asyncIterator]`、`locked` |
| `ReadableStreamDefaultReader` | `read`、`cancel`、`releaseLock`、`closed` |
| `ReadableStreamDefaultController` | `enqueue`、`close`、`error`、`desiredSize` |
| `WritableStream` | underlying sink 的 `start` / `write` / `close` / `abort`；`getWriter`、`close`、`abort`、`locked` |
| `WritableStreamDefaultWriter` | `write`、`close`、`abort`、`releaseLock`、`closed`、`ready`、`desiredSize` |
| `WritableStreamDefaultController` | `error` |
| `TransformStream` | transformer 的 `start` / `transform` / `flush`；`readable`、`writable` |
| `TransformStreamDefaultController` | `enqueue`、`error`、`terminate`、`desiredSize` |
| `TextEncoderStream` | 输入字符串块，输出 UTF-8 `Uint8Array` 块 |
| `TextDecoderStream` | 输入 `Uint8Array` 块，输出字符串块 |
| `CountQueuingStrategy` / `ByteLengthQueuingStrategy` | `highWaterMark` 与 `size(chunk)` |

`ReadableStream.from(iterable)` 可从数组、字符串或任何同步/异步可迭代对象创建可读流。构造函数是一等值，因此 `const RS = ReadableStream; new RS(...)`、`ReadableStream.from(...)` 与 `stream instanceof ReadableStream` 均可用。

### 示例

```ts
async function main(): Promise<void> {
  const transform = new TransformStream<string, string>({
    transform(chunk, controller) {
      controller.enqueue(chunk.toUpperCase());
    },
  });
  const writer = transform.writable.getWriter();
  const reader = transform.readable.getReader();

  const first = reader.read();
  await writer.write("hello");
  console.log(JSON.stringify(await first)); // {"value":"HELLO","done":false}
  const done = reader.read();
  await writer.close();
  console.log(JSON.stringify(await done)); // {"done":true}
}
main();
```

## 异步模型

`start`、`pull`、`write`、`close`、`abort`、`cancel`、`transform` 与 `flush` 都可以返回 Promise。流机制会把它接到运行时 Promise 队列上：等待定时器或 socket 的 `pull` 会让事件循环继续工作，慢速 sink 会串行化写入而不阻塞事件循环。读写方法返回 Promise；空流上的 `reader.read()` 会保持 pending，直到 `pull`（或其他生产者）入队数据。

背压通过 `desiredSize` 与 `writer.ready` 体现：只要 reader 有需求且可读队列低于 high-water mark 就会调用 `pull`；可写队列满时 `writer.ready` 保持 pending。

## 限制

- **不支持字节流 / BYOB。** `getReader({ mode: "byob" })` 抛出 `TypeError`；`ReadableStreamBYOBReader` 与 `ReadableStreamBYOBRequest` 只有名字，构造函数会抛错。
- **不支持压缩流。** `node:stream/web` 会导出 `CompressionStream` 与 `DecompressionStream`，但构造函数抛出明确错误。
- `TextDecoderStream` 按块独立解码，跨块拆分的 UTF-8 序列不会重新拼接。
- `tee` 的两个分支没有独立背压；无界源 + 慢分支会持续累积数据。
- `pipeTo` 忽略 `preventClose` / `preventAbort` / `signal` 选项。
- 这些类是普通运行时对象；Web IDL 品牌检查、`postMessage` 转移以及 `fetch` 响应的 `ReadableStream` 所有权尚未实现。
