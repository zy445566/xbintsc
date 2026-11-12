# `tls`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/tls.md)

Location: `src/extensions/node/tls/`, `runtime/ext_node/tls/`, `runtime/xt_tls/`

`node:tls` is backed by vendored mbedTLS (TLS 1.2/1.3, X.509 and PSA crypto).
Two link features keep it optional:

- **`tls`** (declared by the core extension) is the mbedTLS transport:
  `runtime/xt_tls/tls_core.c` is archived into `tls.{a,lib}` and the feature's
  compiled entry point is the tiny `tls_bridge.c`, whose call extracts the
  backend and installs it. `fetch` uses it for `https://` URLs. It is selected
  by an `https://` literal, `node:tls`/`node:http2` usage, or `--feature tls`.
- **`node-tls`** (`requires: ["tls"]`) adds the JavaScript surface in
  `runtime/ext_node/tls/tls_backend.c` and is selected by importing `tls` /
  `node:tls` (the import specifier is a string literal in the bundle).

`npm run tls` fetches the pinned mbedTLS release into `vendor/mbedtls`, compiles
it together with `tls_core.c` and writes `runtime/lib/<os>-<arch>/tls.{a,lib}`.
The bridge and the backend are the only tracked sources, so an installed npm
package compiles a TLS-using program without the vendored headers. Without the
archive the modules still import, but the calls throw a clear runtime error;
`--feature tls` turns the missing archive into a build error.

| API | Notes |
| --- | --- |
| `tls.connect(options[, callback])` | `options`: `port`, `host`, `servername`, `ca`, `cert`, `key`, `rejectUnauthorized`, `ALPNProtocols`. Also `connect(port[, host][, options][, callback])` |
| `tls.createServer(options[, onSecure])` | `options`: `cert`, `key`, `ca`, `requestCert`, `rejectUnauthorized`, `ALPNProtocols`; reuses `net.Server` for `listen` / `address` / `close` |
| `tls.TLSSocket` | EventEmitter with `write` / `end` / `destroy` / `address` / `setEncoding` / `getPeerCertificate`; emits `data`, `end`, `close`, `error` |

`connect` performs the TCP connect and TLS handshake synchronously, then returns
an already-secure socket. `secureConnect` and the callback are delivered on a
zero-delay timer, so they run after the `const socket = connect(...)` binding is
initialised, matching Node's ordering. The socket's readiness watcher decrypts
records on the reactor; writes block only for the duration of one record.

## Certificate verification

- `rejectUnauthorized` defaults to `true`. A supplied `ca` (PEM string or
  Buffer) replaces the platform trust store; without one, Windows reads its
  `ROOT` store through crypt32, Linux reads `ca-certificates.crt` /
  `ca-bundle.crt` (or `/etc/ssl/certs`), and macOS/BSD read
  `/etc/ssl/cert.pem`.
- `NODE_EXTRA_CA_CERTS` names a PEM bundle trusted **in addition** to the
  platform roots, like Node. The `tls` e2e tests use it with a self-signed
  fixture.
- `servername` defaults to `host` and is used for SNI and hostname verification.

## Known limits

`createServer` looks exactly like `net.Server` but performs the handshake on the
accept path (the runtime has no separate TLS thread). There is no session
resumption, 0-RTT, or TLS-over-a-preconnected-`net`-socket constructor: a
`TLSSocket` wraps a connection the module itself opened or accepted. PEM inputs
must be strings or Buffers; `key` and `cert` must be supplied together.
