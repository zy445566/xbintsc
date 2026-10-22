# `worker_threads`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/worker-threads.md)

Location: `src/extensions/node/worker_threads/index.ts`, `runtime/ext_node/worker_threads/worker_threads.c`

- `Worker`, `isMainThread`, `workerData`, `parentPort`.
- xbintsc emulates a worker by re-running the current executable as a child
  process; `isMainThread` / `workerData` / `parentPort` are value bindings
  (nullary runtime getters) and `Worker` is a constructor.
