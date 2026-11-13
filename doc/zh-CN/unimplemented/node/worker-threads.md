# `worker_threads`（未实现）

> 属于[Node 扩展：未实现](../node.md) · [English](../../../unimplemented/node/worker-threads.md)

传输层是第二个进程而不是操作系统线程，端口也只存在于单个进程内，因此以下内容
尚未实现：

- 把 `MessagePort` / `ArrayBuffer` 转移给另一个 worker 或父进程
  （`postMessage(value, [port])`）。transfer 列表被接受但按复制处理，发送端口会抛出
  `MessagePort could not be cloned.`。
- `BroadcastChannel`、`setEnvironmentData` / `getEnvironmentData`、`SHARE_ENV`
  与 `resourceLimits`。
- 真正的并行：worker 是进程，值只复制，不能让 `SharedArrayBuffer` 共享内存。
  worker 始终运行当前程序（`import.meta.filename`），不能运行任意脚本文件。
- worker 的 `"error"` 事件：worker 内未捕获的异常会打印到该进程的 stderr 并以
  非零码结束，父进程只看到 `"exit"`。
- `worker.unref()` / `worker.ref()` 被接受，但不会改变父进程事件循环是否保持存活
  （运行中的 worker 会让它保持存活）。
- `receiveMessageOnPort` 返回 `undefined`；本地端口本来就在零延时定时器上投递队列。
- `workerData` 通过环境变量传递，因此在 Windows 上上限为 32 KiB。
