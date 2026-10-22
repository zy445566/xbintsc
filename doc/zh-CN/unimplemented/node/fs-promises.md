# `fs/promises` 模块的缺口

> 属于[Node 扩展：未实现](../node.md) · [English](../../../unimplemented/node/fs-promises.md)

`fs/promises` 现在把阻塞 I/O offload 到异步 I/O 线程池（已实现表面见[已实现：`fs/promises`](../../implemented/node/fs-promises.md)）；剩余差异主要是行为层面的：

- `FileHandle.appendFile` 行为等同 `writeFile`（按当前文件位置写入，而不是追加）。
- `FileHandle.readFile()` 从当前描述符偏移读取。
- `Dir.read` / `Dir.close`（同步与 Promise 形式）立即调用回调；`Dir.readSync` / `closeSync` 仍是同步的。
- `watch` resolve 为与 `fs.watch` 相同的、永不触发的 watcher；没有真正的文件监听后端。
- 读写是一次性整体操作；没有流式 `FileHandle` 变体，也不支持 `AbortSignal` 取消。
- 线程池固定为 4 个 worker；大量并发操作会排队（但仍不会阻塞事件循环）。
