# `http`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/http.md)

Location: `src/extensions/node/http/index.ts`, `runtime/ext_node/http/http.c`

The server wraps a `net` server: each connection accumulates bytes until a
complete request (request line + headers + `Content-Length` body) is available,
then invokes the `request` listener with `req`/`res`. The client wraps a `net`
socket, writes an HTTP/1.1 request and parses the response after the connection
closes.

| API | Notes |
| --- | --- |
| `http.createServer([requestListener])` | create an HTTP server |
| `http.request(options[, cb])` | create a `ClientRequest` (`write` / `end` / `setHeader`) |
| `http.get(url[, cb])` | issue a GET |

`IncomingMessage` (`req` / response): `method`, `url`, `httpVersion`, `headers`, `statusCode`, `data` / `end` events, `setEncoding`.

`ServerResponse` (`res`): `writeHead(status[, message][, headers])`, `setHeader` / `getHeader` / `removeHeader` / `getHeaders`, `write(chunk)`, `end([chunk])`, events `finish` / `close`. Responses always carry `Connection: close` (no keep-alive).
