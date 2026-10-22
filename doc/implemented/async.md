# Async, promises and generators

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/async.md)

Location: `runtime/xt_async.c`, `runtime/xt_coroutine.c`, `runtime/xt_promise.c`,
`runtime/xt_generator.c`, `runtime/xt_loop.c`, `runtime/xt_async_io.c`,
`runtime/xt_performance.c`.

## async / await

- `async` closures are marked with `xt_function_set_async`; calling one runs the body on a stackful coroutine (`xt_coroutine.c`, `xt_async.c`) and immediately returns a promise.
- The body runs synchronously until the first `await` of a pending promise, then suspends; settlement queues a microtask that resumes the coroutine.
- An exception that escapes the body rejects that promise instead of throwing synchronously.
- `await` on a non-promise or settled promise keeps the synchronous fast path and raises on rejection.
- Each `async` call owns a private stack and swaps its `try` frame stack and GC stack region, so `await` survives `try`/`finally`, loops and closures. A promise settled from a timer, a socket callback, an offloaded file operation or another coroutine resumes the suspended body on the microtask queue.

## async generators

- `async function*` closures are marked with `xt_function_set_async_generator`; each `.next()` / `.throw()` / `.return()` enqueues a request and returns a promise.
- The body runs on the generator coroutine and may suspend at either `yield` or `await`; `xt_generator_await` parks it on the awaited promise and leaves the current request pending until the next yield or completion.
- `for await...of` lowers to `xt_async_iter_open` / `xt_async_iter_next` + `xt_await`, so it consumes both async iterables and the synchronous iteration protocol. `Symbol.asyncIterator` is supported.

## Generators

- `function*` / `yield` / `yield*`, with `.next()` / `.throw()` / `.return()`, is implemented on the same stackful-coroutine runtime.
- `for...of`, spread and delegation are supported.

## Promise

- Microtask queue (`xt_microtasks`); `xt_promise_ctor/resolve/reject/static` (including `withResolvers`), instance `then` / `catch` / `finally`.
- `xt_drain_microtasks` empties the queue at program exit, and the event loop drains it between callbacks.
- `xt_await` queues a microtask for a non-promise or settled value, and suspends the running async coroutine on a pending promise until it settles.

## Timers and the event loop

- `setTimeout` / `clearTimeout` / `setInterval` / `clearInterval` / `setImmediate` / `clearImmediate` / `queueMicrotask` and `performance.now` (monotonic fractional milliseconds).
- Timers use numeric ids; `Timeout` / `Immediate` objects are not implemented.
- A promise settled from a timer can be awaited: the async body suspends on the pending promise and resumes when the timer fires. Sockets and servers (`net`, `http`, `dgram`) deliver their callbacks on the same loop.
- `fetch` and `fs/promises` are reactor-driven rather than blocking — see the next section.

## Asynchronous I/O offload

- `runtime/xt_async_io.c` owns a small pool of worker threads (four by default, created lazily). Jobs are split in two halves: a `run` function on a worker that may only touch plain C memory, and a `complete` function on the main thread that builds values and settles the promise. The split is what keeps the collector and the value model single-threaded.
- A completed job wakes `select(2)` through a self-pipe (POSIX) or a loopback UDP socket (Windows); the reactor also polls the pool, so a failed wakeup degrades to a short wait instead of a hang.
- While a job is queued or running, up to four values per job (its promise, and for `fetch` the headers object) are kept in the job's root slots and scanned by the collector.
- `fetch` runs each request/redirect hop as a job; `fs/promises` runs every file and descriptor operation as a job. Neither blocks timers, sockets or other `await`s.
- The event loop stays alive while jobs are outstanding (`xt_loop_has_work`), and exits once the pool is idle again.

Known limits of the asynchronous surface (no TLS/streaming `fetch`, inert
`fs.watch`, four-worker pool) are listed in
[Runtime deviations](../unimplemented/deviations.md) and the Node pages.
