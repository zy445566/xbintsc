# `dgram`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/dgram.md)

Location: `src/extensions/node/dgram/index.ts`, `runtime/ext_node/dgram/dgram.c`

UDP sockets. `dgram.createSocket(type | options[, cb])` returns an EventEmitter.

| Method | Notes |
| --- | --- |
| `bind([port][, address][, cb])` | bind (`send` auto-binds if unbound) |
| `send(msg[, offset, length,] port[, address][, cb])` | send a datagram |
| `close([cb])` / `address()` | close / query address |
| `setBroadcast(b)` / `setTTL(n)` / `setMulticastTTL(n)` | socket options |
| `on('message', (msg, rinfo) => ...)` | receive a datagram; `rinfo` has `address` / `port` / `family` / `size` |
