# `fs/promises` 模块的缺口

> 属于[Node 扩展：未实现](../node.md) · [English](../../../unimplemented/node/fs-promises.md)

`fs/promises` 把每个同步 `fs` 调用包进一个已 settle 的 Promise（已实现表面见
[已实现：`fs/promises`](../../implemented/node/fs-promises.md)）。

- I/O 是同步的：Promise 在 `await` 之前就已 settle，因此 await 它不会让出事件循环。
- `FileHandle.appendFile` 行为等同 `writeFile`（按当前文件位置写入，而不是追加）。
- `FileHandle.readFile()` 从当前描述符偏移读取。
- `Dir.read` / `Dir.close`（同步与 Promise 形式）立即完成。
- `watch` resolve 为与 `fs.watch` 相同的、永不触发的 watcher；没有真正的文件监听后端。
