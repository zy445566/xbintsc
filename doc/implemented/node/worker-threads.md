# `worker_threads`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/worker-threads.md)

Location: `src/extensions/node/worker_threads/index.ts`, `runtime/worker/`

xbintsc has no threads (the value model, the collector and the event loop are
single-threaded), so a worker is a **second process** running the same
executable. The worker entry is therefore the current program: pass
`import.meta.filename` and branch on `isMainThread`.

```ts
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

if (isMainThread) {
  const worker = new Worker(import.meta.filename, { workerData: { n: 2 } });
  worker.on("message", (reply) => {
    console.log(reply.total);
    worker.terminate();
  });
  worker.postMessage({ values: [1, 2, 3] });
} else {
  parentPort.on("message", (message) => {
    const values: number[] = message.values;
    parentPort.postMessage({ total: values[0] + values[1] + values[2] });
  });
}
```

## Surface

- `Worker(script, options?)` — `options.workerData` (structured-cloned),
  `options.name`; `script` must name this executable or the constructor throws
  an actionable error. `options.env`, `argv`, `execArgv`, `resourceLimits`,
  `stdin`/`stdout`/`stderr` are accepted but ignored.
- `worker.postMessage(value, transferList?)` — the value is structured-cloned;
  the transfer list is accepted but copies. Sending a function, symbol,
  promise, `WeakMap`/`WeakSet`/`WeakRef`, generator or `MessagePort` throws
  `... could not be cloned.`
- `worker.on("message", ...)` / `worker.once` / `off` / `removeAllListeners` —
  `message` delivers the raw value.
- `worker.on("online" | "error" | "exit", ...)` — `online` after the control
  channel is accepted, `exit` with the process exit code (a terminated worker
  reports `1`). `messageerror` is emitted when a frame cannot be decoded.
- `worker.terminate(): Promise<number>` — kills the process, resolves with the
  exit code; `ref()`/`unref()` are accepted as no-ops.
- `worker.threadId` — a per-process worker id.
- `isMainThread`, `workerData` (null on the main thread), `parentPort` (null on
  the main thread) with `postMessage`, `on("message")`, `close()`, `start()`,
  `ref()`/`unref()`.
- `new MessageChannel()` / `MessagePort` — a local channel: `port1.postMessage`
  is delivered to `port2` on a zero-delay timer, and either port accepts
  `on("message")` (raw value) or `onmessage` / `addEventListener` (a
  `MessageEvent`). `receiveMessageOnPort` exists but a local port already
  delivers on a timer, so it returns `undefined`.
- `markAsUncloneable(value)` — accepted; the codec already rejects streams and
  host handles.

## Message data

Messages and `workerData` use the structured-clone codec in
`runtime/worker/worker_codec.c`: `undefined`, booleans, numbers (including
`NaN`, `±Infinity`, `-0`), strings, `BigInt`, arrays, plain objects, `Map`,
`Set`, `Date`, `RegExp`, `ArrayBuffer`, typed arrays, shared references and
cycles. Classes arrive as plain objects; functions and symbols are rejected.

## Lifetime and errors

The parent stays alive while a worker runs. A worker stays alive while it has a
`message` listener (or other event-loop work); a worker that posts once and
returns exits on its own, so `worker.on("message")` plus `terminate()` is the
usual pattern for a long-lived worker. A worker that returns closes its control
channel before the process ends, so a message posted just before exit is still
delivered. An uncaught exception in the worker is printed by that process on
stderr and ends it with a non-zero code; the parent observes `exit`, not
`error`. Closing the parent's last handle ends the worker through the socket
EOF.

## Platform notes

The transport is a loopback TCP socket (Windows `select(2)` only accepts
sockets, so pipes would need a second implementation). POSIX uses
`fork`/`execvp` and `waitpid(WNOHANG)`; Windows uses `_spawnvp(_P_NOWAIT)` and
`WaitForSingleObject` on the process handle. The port and a random handshake
token travel through the environment, which caps `workerData` at 32 KiB on
Windows.

## Limitations

`MessagePort` transfer between processes, `BroadcastChannel`,
`setEnvironmentData`/`getEnvironmentData`, `SHARE_ENV` and real thread
parallelism are not implemented — see
[Node extension: unimplemented](../../unimplemented/node.md).
