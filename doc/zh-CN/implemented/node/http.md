# `http` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/http.md)

实现位置：`src/extensions/node/http/index.ts`、`runtime/ext_node/http/http.c`

服务端包裹一个 `net` 服务端：每个连接累积字节直到完整请求（请求行 + 头 + `Content-Length` body）可用，再以 `req`/`res` 调用 `request` 监听器。客户端包裹一个 `net` 套接字，写出 HTTP/1.1 请求并在连接关闭后解析响应。

| API | 说明 |
| --- | --- |
| `http.createServer([requestListener])` | 创建 HTTP 服务端 |
| `http.request(options[, cb])` | 创建 `ClientRequest`（`write` / `end` / `setHeader`） |
| `http.get(url[, cb])` | 发起 GET |

`IncomingMessage`（`req` / 响应）：`method`、`url`、`httpVersion`、`headers`、`statusCode`、`data` / `end` 事件、`setEncoding`。

`ServerResponse`（`res`）：`writeHead(status[, message][, headers])`、`setHeader` / `getHeader` / `removeHeader` / `getHeaders`、`write(chunk)`、`end([chunk])`，事件 `finish` / `close`。响应固定带 `Connection: close`（不做 keep-alive）。
