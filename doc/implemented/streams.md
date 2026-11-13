# Web Streams

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/streams.md)

Location: `runtime/xt_streams.c` and `runtime/xt_streams/`. The classes are core
runtime globals, so they are available without any extension flag; Node's
`stream/web` module re-exports the same constructors (see
[stream/web](./node/stream-web.md)).

## Classes

| Class | Notes |
| --- | --- |
| `ReadableStream` | underlying source `start` / `pull` / `cancel`; `getReader`, `cancel`, `tee`, `pipeTo`, `pipeThrough`, `values`, `[Symbol.asyncIterator]`, `locked` |
| `ReadableStreamDefaultReader` | `read`, `cancel`, `releaseLock`, `closed` |
| `ReadableStreamDefaultController` | `enqueue`, `close`, `error`, `desiredSize` |
| `WritableStream` | underlying sink `start` / `write` / `close` / `abort`; `getWriter`, `close`, `abort`, `locked` |
| `WritableStreamDefaultWriter` | `write`, `close`, `abort`, `releaseLock`, `closed`, `ready`, `desiredSize` |
| `WritableStreamDefaultController` | `error` |
| `TransformStream` | transformer `start` / `transform` / `flush`; `readable`, `writable` |
| `TransformStreamDefaultController` | `enqueue`, `error`, `terminate`, `desiredSize` |
| `TextEncoderStream` | string chunks in, UTF-8 `Uint8Array` chunks out |
| `TextDecoderStream` | `Uint8Array` chunks in, string chunks out |
| `CountQueuingStrategy` / `ByteLengthQueuingStrategy` | `highWaterMark` and `size(chunk)` |

`ReadableStream.from(iterable)` builds a readable stream from an array, string
or any synchronous or asynchronous iterable. The constructors are first-class
values, so
`const RS = ReadableStream; new RS(...)`, `ReadableStream.from(...)` and
`stream instanceof ReadableStream` all work.

### Example

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

## Async model

`start`, `pull`, `write`, `close`, `abort`, `cancel`, `transform` and `flush`
may return a promise. The stream machinery chains it through the runtime promise
queue, so a `pull` that waits for a timer or socket keeps the event loop alive
and a slow sink serialises writes without blocking it. Reader and writer methods
return promises; `reader.read()` on an empty stream stays pending until `pull`
(or another producer) enqueues a chunk.

Backpressure is honoured through `desiredSize` and `writer.ready`: `pull` is
called while a reader has demand and the readable queue is below its
high-water mark, and `writer.ready` stays pending while the writable queue is
full.

## Limits

- **No byte streams / BYOB.** `getReader({ mode: "byob" })` throws
  `TypeError`; `ReadableStreamBYOBReader` and `ReadableStreamBYOBRequest` exist
  as names but their constructors throw.
- **No compression streams.** `CompressionStream` and `DecompressionStream` are
  exported by `node:stream/web` but their constructors throw a clear error.
- `TextDecoderStream` decodes each chunk independently, so a UTF-8 sequence
  split across two chunks is not reassembled.
- `tee` buffers into the two branches without independent backpressure; for an
  unbounded source the slower branch accumulates chunks.
- `pipeTo` ignores `preventClose` / `preventAbort` / `signal` options.
- The classes are ordinary runtime objects; the Web IDL brand checks, transfer
  (`postMessage`) and `ReadableStream` ownership in `fetch` responses are not
  implemented.
