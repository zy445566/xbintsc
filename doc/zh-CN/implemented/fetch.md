# fetch

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/fetch.md)

实现位置：`runtime/xt_fetch.c`。

- 全局 `fetch(input, init)` 是阻塞式 HTTP/1.1 客户端，返回的 promise 已经 settle（交换同步完成，不经过 reactor 调度）。
- 只接受 `http://` URL —— 由于未链接 TLS 后端，`https://` 会以 `TypeError` 拒绝。
- 选项：`method`、`headers`（普通对象或 `Headers`）、`body` 与 `redirect`（`follow` / `manual` / `error`）；最多跟随 20 次重定向，并在 301/302/303 时将 POST 改为 GET。
- 解析出的 `Response` 形状对象暴露 `ok` / `status` / `statusText` / `url` / `headers` 以及 `text()` / `json()` / `arrayBuffer()` / `bytes()` / `clone()`。
- `Headers` 大小写不敏感，提供 `get` / `has` / `set` / `append` / `delete` / `keys` / `values` / `entries` / `forEach` / `getSetCookie`。
- 非法 URL 与网络失败会以 `TypeError` 拒绝。
