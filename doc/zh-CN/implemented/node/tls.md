# `tls` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/tls.md)

实现位置：`src/extensions/node/tls/`、`runtime/ext_node/tls/`、`runtime/xt_tls/`

`node:tls` 由随仓库引入（vendored）的 mbedTLS 实现（TLS 1.2/1.3、X.509 与 PSA crypto）。
两个链接特性使它保持可选：

- **`tls`**（由核心扩展声明）是 mbedTLS 传输层：`runtime/xt_tls/tls_core.c`
  被归档进 `tls.{a,lib}`，该特性编译的入口是极小的 `tls_bridge.c`，它的调用会取出
  后端并完成安装。`fetch` 的 `https://` 请求使用它。出现 `https://` 字面量、使用
  `node:tls` / `node:http2`，或传入 `--feature tls` 时会选中。
- **`node-tls`**（`requires: ["tls"]`）在 `runtime/ext_node/tls/tls_backend.c`
  中提供 JavaScript 接口，导入 `tls` / `node:tls` 时选中（导入说明符在打包后是字符串字面量）。

`npm run tls` 把固定版本的 mbedTLS 下载到 `vendor/mbedtls`，与 `tls_core.c`
一起编译，并写出 `runtime/lib/<os>-<arch>/tls.{a,lib}`。只有 bridge 与后端是受版本控制
的源码，因此已安装的 npm 包无需 vendor 头文件即可编译使用 TLS 的程序。缺少归档时模块
仍可导入，但调用会抛出明确的运行时错误；显式 `--feature tls` 则会让缺失的归档变成构建错误。

| API | 说明 |
| --- | --- |
| `tls.connect(options[, callback])` | `options`：`port`、`host`、`servername`、`ca`、`cert`、`key`、`rejectUnauthorized`、`ALPNProtocols`；也支持 `connect(port[, host][, options][, callback])` |
| `tls.createServer(options[, onSecure])` | `options`：`cert`、`key`、`ca`、`requestCert`、`rejectUnauthorized`、`ALPNProtocols`；`listen` / `address` / `close` 复用 `net.Server` |
| `tls.TLSSocket` | EventEmitter，含 `write` / `end` / `destroy` / `address` / `setEncoding` / `getPeerCertificate`；触发 `data`、`end`、`close`、`error` |

`connect` 同步完成 TCP 连接与 TLS 握手，返回的套接字已经安全。`secureConnect`
与回调在零延迟定时器中触发，因此晚于 `const socket = connect(...)` 的赋值，
与 Node 的时序一致。套接字的可读事件在 reactor 上解密记录；写入只在一个记录期间阻塞。

## 证书校验

- `rejectUnauthorized` 默认为 `true`。传入的 `ca`（PEM 字符串或 Buffer）会替换平台
  信任库；未传时，Windows 通过 crypt32 读取 `ROOT` 存储，Linux 读取
  `ca-certificates.crt` / `ca-bundle.crt`（或 `/etc/ssl/certs`），macOS/BSD 读取
  `/etc/ssl/cert.pem`。
- `NODE_EXTRA_CA_CERTS` 指向的 PEM 包会在平台根证书之外**额外**被信任，与 Node 一致。
  `tls` 端到端测试用自签名证书夹具配合它运行。
- `servername` 默认取 `host`，用于 SNI 与主机名校验。

## 已知限制

`createServer` 的外观与 `net.Server` 一致，但在 accept 路径上完成握手（运行时没有独立的
TLS 线程）。不支持会话恢复、0-RTT，也没有基于已连接 `net` 套接字的 `TLSSocket`
构造器：`TLSSocket` 只包裹本模块建立或接受的连接。PEM 输入必须是字符串或 Buffer；
`key` 与 `cert` 必须同时提供。
