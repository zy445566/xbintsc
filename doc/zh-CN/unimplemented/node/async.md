# 异步与事件循环模型

> 属于[Node 扩展：未实现](../node.md) · [English](../../../unimplemented/node/async.md)

已实现部分：

- 核心事件循环 `runtime/xt_loop.c`（`select(2)` 反应堆），`net` / `dgram` / `http` 均构建于其上。
- 异步 I/O offload `runtime/xt_async_io.c`：阻塞的文件与 HTTP 客户端工作在 4 个工作线程上执行，并在 reactor 上完成，因此 `fs/promises` 与 `fetch` 不会卡住定时器或套接字。
- `Promise`、`async` / `await` 已可用（见核心文档），`fs/promises` 返回从 reactor settle 的 Promise。
- 定时器已作为全局函数提供：`setTimeout` / `clearTimeout` / `setInterval` / `clearInterval` 运行在核心反应堆上，并在仍有定时器时保持进程存活。
- 提供独立的 `EventEmitter`（`events` 模块，同时也是全局构造函数）；流与套接字额外具备内部发射器方法（`on` / `addListener` / `once` / `off` / `removeListener` / `emit`）。

仍未实现：

- Node 的 `Timeout` 对象（`setTimeout` 返回数字 id；`setImmediate` 与 `queueMicrotask` 已作为全局函数提供）。
- 文件监听：`fs.watch` / `fs/promises.watch` 返回惰性对象；没有 inotify/kqueue/ReadDirectoryChangesW 后端。
- `Dir.read` / `Dir.close` 立即调用回调，回调式 `fs.readFile` / `fs.writeFile` / … 仍不支持（请用 `fs/promises` 或 `*Sync` 形式）。
- 流与套接字的内置 `once` 等同 `on`（不具「触发一次后移除」语义）；`events` 模块的 `EventEmitter` 实现了真正的 `once`。
- `net` 的 `connect` 仍是在套接字加入 reactor 之前执行的阻塞 `connect(2)`；HTTP 响应要求 `Connection: close`，不做 keep-alive / 分块传输 / 流水线复用。`fetch` 没有 TLS，且在 resolve 前缓冲整个响应体。
- offload 线程池固定为 4 个 worker；没有线程池大小选项，也不支持对排队 job 的 `AbortSignal` 取消。
