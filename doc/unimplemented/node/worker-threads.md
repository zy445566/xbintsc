# `worker_threads` (unimplemented)

> Part of [Node extension: unimplemented](../node.md) · [简体中文](../../zh-CN/unimplemented/node/worker-threads.md)

The transport is a second process, not an OS thread, and a port is local to one
process. The following are therefore missing:

- Transferring a `MessagePort` / `ArrayBuffer` to another worker or the parent
  (`postMessage(value, [port])`). The transfer list is accepted but values are
  copied, and sending a port throws `MessagePort could not be cloned.`
- `BroadcastChannel`, `setEnvironmentData` / `getEnvironmentData`,
  `SHARE_ENV` and `resourceLimits`.
- Real parallelism: workers are processes, so values are copied and
  `SharedArrayBuffer` cannot be shared. The worker always runs the current
  program (`import.meta.filename`), not an arbitrary script file.
- Worker `"error"` events: an uncaught exception in the worker prints on that
  process's stderr and ends it with a non-zero code; the parent sees `"exit"`.
- `worker.unref()` / `worker.ref()` are accepted but do not change whether the
  parent's event loop stays alive (a running worker keeps it alive).
- `receiveMessageOnPort` returns `undefined`; a local port already delivers its
  queue on a zero-delay timer.
- `workerData` travels through the environment, so it is limited to 32 KiB on
  Windows.
