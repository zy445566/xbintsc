# `net`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/net.md)

Location: `src/extensions/node/net/index.ts`, `runtime/ext_node/net/net.c`

TCP server and client, built on the core event loop.

| API | Notes |
| --- | --- |
| `net.createServer([connectionListener])` | create a TCP server (equivalent to the `Server` constructor) |
| `net.connect(...)` / `net.createConnection(...)` | connect (blocking connect, then registers with the event loop) |
| `net.isIP(s)` / `net.isIPv4(s)` / `net.isIPv6(s)` | address test |

`Server`: `listen(port[, host][, cb])`, `close([cb])`, `address()`, `getConnections(cb)`, events `listening` / `connection` / `close`.

`Socket`: `write(data[, cb])`, `end([data])`, `destroy()`, `address()`, `setEncoding(enc)`, `pause()` / `resume()`, events `data` / `end` / `close` / `connect` / `error`.
