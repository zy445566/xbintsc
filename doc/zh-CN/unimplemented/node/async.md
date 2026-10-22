# 异步与事件循环模型

> 属于[Node 扩展：未实现](../node.md) · [English](../../../unimplemented/node/async.md)

已实现部分：

- 核心事件循环 `runtime/xt_loop.c`（`select(2)` 反应堆），`net` / `dgram` / `http` 均构建于其上。
- `Promise`、`async` / `await` 已可用（见核心文档），`fs/promises` 返回 Promise。
- 定时器已作为全局函数提供：`setTimeout` / `clearTimeout` / `setInterval` / `clearInterval` 运行在核心反应堆上，并在仍有定时器时保持进程存活。
- 提供独立的 `EventEmitter`（`events` 模块，同时也是全局构造函数）；流与套接字额外具备内部发射器方法（`on` / `addListener` / `once` / `off` / `removeListener` / `emit`）。

仍未实现：

- Node 的 `Timeout` 对象（`setTimeout` 返回数字 id；`setImmediate` 与 `queueMicrotask` 已作为全局函数提供）。
- 真正的异步 I/O 调度：`fs/promises` 实质是同步操作的即时 settle 包装，不会在等待 I/O 时让出。
- 流与套接字的内置 `once` 等同 `on`（不具「触发一次后移除」语义）；`events` 模块的 `EventEmitter` 实现了真正的 `once`。
- `net` 的 `connect` 为阻塞式；HTTP 响应要求 `Connection: close`，不做 keep-alive / 分块传输 / 流水线复用。
