# `http2` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/http2.md)

实现位置：`src/extensions/node/http2/`、`runtime/ext_node/http2/`、`runtime/xt_http2/`

HTTP/2 分帧与 HPACK 来自随仓库引入的 nghttp2。导入 `http2` / `node:http2`
（或出现 `createSecureServer`）时选中 `http2` 链接特性；它 `requires:
["tls", "node-tls"]`，因为安全服务端复用 `node:tls` 的服务端封装，两种方案都通过
mbedTLS 协商 ALPN `h2`。`runtime/ext_node/http2/http2_backend.c` 被编译进固定版本的 nghttp2 归档
（`runtime/lib/<os>-<arch>/nghttp2.{a,lib}`）；该特性编译的入口是
`http2_bridge.c`，它的调用会取出后端并完成安装。`npm run tls` 会同时构建该归档与
mbedTLS 归档，因此已安装的 npm 包不需要 vendor 头文件。

一个 `Http2Session` 拥有一个套接字和一个 `nghttp2_session`。运行时的可读 reactor
把字节送入 `nghttp2_session_mem_recv2` 并冲刷 `nghttp2_session_mem_send2` 的输出；
nghttp2 回调负责触发 JavaScript 事件。没有 HTTP/1.1 upgrade：明文 `http://` 连接
直接使用 h2c prior knowledge，与 Node 客户端一致。

| API | 说明 |
| --- | --- |
| `http2.connect(authority[, options][, listener])` | `http(s)://host:port`；`options`：`ca`、`rejectUnauthorized`；返回已连接的 `ClientHttp2Session` 并触发 `connect` |
| `session.request(headers[, options])` | `headers` 含 `:method`（默认 `GET`）、`:path`、`:scheme`、`:authority` 及普通字段；返回 `ClientHttp2Stream` |
| `stream.end([data])` / `write(data)` / `close()` | 请求体与取消；触发 `response(headers, flags)`、`data`、`end`、`close`、`error` |
| `session.close()` / `destroy()` | 发送 GOAWAY 并关闭传输 |
| `http2.createServer([options][, onRequest])` | 明文 h2c 服务端；`listen` / `address` / `close` 与 `net.Server` 一致 |
| `http2.createSecureServer(options[, onRequest])` | TLS + ALPN `h2`；`options` 需要 `cert` 与 `key`（客户端证书再加 `ca`） |
| `server.on("stream", (stream, headers) => …)` | 服务端 stream 同时具有请求事件与 `respond` / `setHeader` / `write` / `end` |
| `http2.constants` | 帧类型与错误码数值（`NGHTTP2_FLAG_END_STREAM`、`NGHTTP2_CANCEL` 等） |

服务端 stream 暴露 `method`、`url`、`scheme`、`authority`、`httpVersion`
（`"2.0"`）与 `headers`；`stream` 事件的第二个参数就是同一个 headers 对象。
`respond(statusOrHeaders[, headers])`（或 `respond({":status": 201})`）、
`setHeader` / `getHeader` 与 `write` / `end` 负责提交响应。请求体通过 `data` /
`end` 事件送达，也可在同一个 stream 对象上访问。

端到端测试双向验证互操作性：编译后的客户端连接 Node 的 `http2` 服务端（h2c 与 TLS），
以及 Node 客户端连接编译后的 `createServer` / `createSecureServer`。

## 已知限制

未实现 `pushStream`、trailers、extended CONNECT、会话池 / 连接复用、`stream.priority`
与流控调优。响应头与响应体在 `end()` 之前会缓冲（`end()` 时才提交响应），因此想流式
发送大 body 的处理函数仍可工作，但不会提前冲刷部分响应。
