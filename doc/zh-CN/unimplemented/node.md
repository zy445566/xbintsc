# Node 扩展：未实现

> 属于[未实现语法与功能](../unimplemented.md) · [English](../../unimplemented/node.md)

Node 扩展实现了 21 个模块以及事件循环集成；已实现表面按模块记录在
[Node 扩展：已实现](../implemented/node.md)。本节记录缺失或与 Node 不同之处。

## 目录

- [`fs`](./node/fs.md) —— 回调式异步操作、真正的文件监听、选项回调
- [`fs/promises`](./node/fs-promises.md) —— 已 settle 的 Promise 背后仍是同步 I/O、`FileHandle` 偏差
- [完全未实现的模块](./node/modules.md) —— `https`、`readline`、`tls`、`cluster`、`vm` 等
- [全局对象与命名空间](./node/globals.md) —— `global`、`__dirname`、`node_modules` 之外的 `require`
- [异步与事件循环模型](./node/async.md) —— 无异步 I/O 调度、阻塞式 `connect`、无 keep-alive
- [Bun 扩展](./node/bun.md) —— 仅为设计示例

模块的已实现页面会链接到对应限制页；限制解除后，把条目移到
`../implemented/node/` 下对应页面。
