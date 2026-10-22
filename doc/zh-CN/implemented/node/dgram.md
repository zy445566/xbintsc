# `dgram` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/dgram.md)

实现位置：`src/extensions/node/dgram/index.ts`、`runtime/ext_node/dgram/dgram.c`

UDP 套接字。`dgram.createSocket(type | options[, cb])` 返回 EventEmitter。

| 方法 | 说明 |
| --- | --- |
| `bind([port][, address][, cb])` | 绑定（未绑定时 `send` 会自动绑定） |
| `send(msg[, offset, length,] port[, address][, cb])` | 发送数据报 |
| `close([cb])` / `address()` | 关闭 / 查询地址 |
| `setBroadcast(b)` / `setTTL(n)` / `setMulticastTTL(n)` | 套接字选项 |
| `on('message', (msg, rinfo) => ...)` | 收到数据报，`rinfo` 含 `address` / `port` / `family` / `size` |
