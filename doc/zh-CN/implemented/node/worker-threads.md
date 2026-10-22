# `worker_threads` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/worker-threads.md)

实现位置：`src/extensions/node/worker_threads/index.ts`、`runtime/ext_node/worker_threads/worker_threads.c`

- `Worker`、`isMainThread`、`workerData`、`parentPort`。
- xbintsc 通过把当前可执行文件作为子进程重新运行来模拟 worker；`isMainThread` /
  `workerData` / `parentPort` 是值绑定（无参运行时 getter），`Worker` 是构造器。
