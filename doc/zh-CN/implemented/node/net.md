# `net` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/net.md)

实现位置：`src/extensions/node/net/index.ts`、`runtime/ext_node/net/net.c`

TCP 服务端与客户端，基于核心事件循环。

| API | 说明 |
| --- | --- |
| `net.createServer([connectionListener])` | 创建 TCP 服务端（`Server` 构造函数等价） |
| `net.connect(...)` / `net.createConnection(...)` | 连接（阻塞式 connect，之后注册事件循环） |
| `net.isIP(s)` / `net.isIPv4(s)` / `net.isIPv6(s)` | 地址判定 |

`Server`：`listen(port[, host][, cb])`、`close([cb])`、`address()`、`getConnections(cb)`，事件 `listening` / `connection` / `close`。

`Socket`：`write(data[, cb])`、`end([data])`、`destroy()`、`address()`、`setEncoding(enc)`、`pause()` / `resume()`，事件 `data` / `end` / `close` / `connect` / `error`。
