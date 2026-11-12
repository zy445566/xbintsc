# `http2`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/http2.md)

Location: `src/extensions/node/http2/`, `runtime/ext_node/http2/`, `runtime/xt_http2/`

HTTP/2 framing and HPACK come from vendored nghttp2. The `http2` link feature is
selected by importing `http2` / `node:http2` (or naming `createSecureServer`);
it `requires: ["tls", "node-tls"]`, because a secure server reuses the
`node:tls` server wrapper and both schemes negotiate ALPN `h2` through mbedTLS.
`runtime/ext_node/http2/http2_backend.c` is compiled into the pinned nghttp2
archive (`runtime/lib/<os>-<arch>/nghttp2.{a,lib}`); the feature's compiled
entry point is `http2_bridge.c`, whose call extracts the backend and installs
it. `npm run tls` builds that archive and the mbedTLS one, so an installed npm
package does not need the vendored headers.

An `Http2Session` owns one socket and one `nghttp2_session`. The runtime's
readiness reactor feeds bytes to `nghttp2_session_mem_recv2` and flushes
`nghttp2_session_mem_send2`; nghttp2 callbacks raise the JavaScript events.
There is no HTTP/1.1 upgrade: a cleartext `http://` connection speaks h2c with
prior knowledge, exactly like Node's client.

| API | Notes |
| --- | --- |
| `http2.connect(authority[, options][, listener])` | `http(s)://host:port`; `options`: `ca`, `rejectUnauthorized`; returns a connected `ClientHttp2Session` and emits `connect` |
| `session.request(headers[, options])` | `headers` with `:method` (default `GET`), `:path`, `:scheme`, `:authority` plus ordinary fields; returns a `ClientHttp2Stream` |
| `stream.end([data])` / `write(data)` / `close()` | request body and cancellation; emits `response(headers, flags)`, `data`, `end`, `close`, `error` |
| `session.close()` / `destroy()` | sends GOAWAY and closes the transport |
| `http2.createServer([options][, onRequest])` | cleartext h2c server; `listen` / `address` / `close` behave like `net.Server` |
| `http2.createSecureServer(options[, onRequest])` | TLS + ALPN `h2`; `options` needs `cert` and `key` (plus `ca` for client certificates) |
| `server.on("stream", (stream, headers) => …)` | server-side stream combines the request emitter with `respond` / `setHeader` / `write` / `end` |
| `http2.constants` | frame-type and error-code numbers (`NGHTTP2_FLAG_END_STREAM`, `NGHTTP2_CANCEL`, …) |

Server-side streams expose `method`, `url`, `scheme`, `authority`,
`httpVersion` (`"2.0"`) and `headers`; the second `stream` argument is the same
headers object. `respond(statusOrHeaders[, headers])` (or `respond({":status":
201})`), `setHeader` / `getHeader` and `write` / `end` submit the response. The
incoming request body is delivered through `data` / `end` and is also available
through the same stream object.

The e2e suite proves interoperability in both directions: a compiled client
against Node's `http2` server (h2c and TLS) and Node's client against a compiled
`createServer` / `createSecureServer`.

## Known limits

`pushStream`, trailers, extended CONNECT, session pooling / connection reuse,
`stream.priority` and flow-control tuning are not implemented. Response headers
and body are buffered until `end()` (which is when the response is submitted),
so a handler that wants to stream a long body still works but does not flush
partial responses early.
