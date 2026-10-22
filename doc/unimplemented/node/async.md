# Async and the event-loop model

> Part of [Node extension: unimplemented](../node.md) · [简体中文](../../zh-CN/unimplemented/node/async.md)

Implemented:

- Core event loop `runtime/xt_loop.c` (a `select(2)` reactor); `net` / `dgram` / `http` are all built on it.
- Async I/O offload `runtime/xt_async_io.c`: blocking file and HTTP-client work runs on four worker threads and completes on the reactor, so `fs/promises` and `fetch` do not stall timers or sockets.
- `Promise`, `async` / `await` are available (see the core documents); `fs/promises` returns Promises that settle from the reactor.
- Timers are available as globals: `setTimeout` / `clearTimeout` / `setInterval` / `clearInterval` run on the core reactor and keep the process alive until no timer remains.
- A standalone `EventEmitter` is provided by the `events` module (also a global constructor); streams and sockets additionally carry internal emitter methods (`on` / `addListener` / `once` / `off` / `removeListener` / `emit`).

Still unimplemented:

- Node's `Timeout` object (`setTimeout` returns a numeric id; `setImmediate` and `queueMicrotask` exist as globals).
- File watching: `fs.watch` / `fs/promises.watch` return inert objects; there is no inotify/kqueue/ReadDirectoryChangesW backend.
- `Dir.read` / `Dir.close` invoke their callbacks immediately, and callback-style `fs.readFile` / `fs.writeFile` / … are still unsupported (use `fs/promises` or the `*Sync` forms).
- On streams and sockets the built-in `once` is equivalent to `on` (no "remove after firing once" semantics); the `events` module's `EventEmitter` does implement real `once`.
- `net`'s `connect` is still a blocking `connect(2)` performed before the socket joins the reactor; HTTP responses require `Connection: close`, with no keep-alive / chunked transfer / pipelining. `fetch` has no TLS and buffers the whole body before resolving.
- The offload pool is fixed at four workers; there is no thread-pool sizing option and no `AbortSignal` cancellation for queued jobs.
