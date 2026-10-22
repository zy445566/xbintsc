# Async and the event-loop model

> Part of [Node extension: unimplemented](../node.md) · [简体中文](../../zh-CN/unimplemented/node/async.md)

Implemented:

- Core event loop `runtime/xt_loop.c` (a `select(2)` reactor); `net` / `dgram` / `http` are all built on it.
- `Promise`, `async` / `await` are available (see the core documents); `fs/promises` returns Promises.
- Timers are available as globals: `setTimeout` / `clearTimeout` / `setInterval` / `clearInterval` run on the core reactor and keep the process alive until no timer remains.
- A standalone `EventEmitter` is provided by the `events` module (also a global constructor); streams and sockets additionally carry internal emitter methods (`on` / `addListener` / `once` / `off` / `removeListener` / `emit`).

Still unimplemented:

- Node's `Timeout` object (`setTimeout` returns a numeric id; `setImmediate` and `queueMicrotask` exist as globals).
- A real asynchronous I/O scheduler: `fs/promises` is essentially an immediate-settle wrapper around synchronous operations and does not yield while waiting for I/O.
- On streams and sockets the built-in `once` is equivalent to `on` (no "remove after firing once" semantics); the `events` module's `EventEmitter` does implement real `once`.
- `net`'s `connect` is blocking; HTTP responses require `Connection: close`, with no keep-alive / chunked transfer / pipelining.
