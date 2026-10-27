# fetch

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/fetch.md)

实现位置：`runtime/xt_fetch.c`、`runtime/xt_fetch/async.inc`、`runtime/xt_async_io.c`。

- 全局 `fetch(input, init)` 是 HTTP/1.1 客户端。阻塞的套接字交换（`getaddrinfo`、connect、send、receive）运行在异步 I/O 工作线程池上，因此进行中的请求**不会**卡住事件循环：定时器、套接字与其他 `await` 继续运行，promise 在 reactor 派发完成结果时 settle。
- 重定向在主线程上处理：每一跳是一次 offload 交换，最多 20 次；301/302/303 会把 POST 改成 GET。
- 只接受 `http://` URL —— 由于未链接 TLS 后端，`https://` 会以 `TypeError` 拒绝。
- **没有协议的引用**（相对路径）与 `file://` URL 会读取本地文件，因此编译后的程序
  无需 HTTP 服务即可加载磁盘资源。相对路径相对工作目录解析；支持 `GET` / `HEAD`。
  在 Windows 上，标准写法 `file:///C:/dir/file` 与 `file://C:/dir/file` /
  `file://C:\dir\file` 都指向同一个文件。
- 选项：`method`、`headers`（普通对象或 `Headers`）、`body` 与 `redirect`（`follow` / `manual` / `error`）。
- 解析出的 `Response` 形状对象暴露 `ok` / `status` / `statusText` / `url` / `headers` 以及 `text()` / `json()` / `arrayBuffer()` / `bytes()` / `clone()`。响应体在 promise settle 前已完整缓冲；不支持流式响应体。
- `Headers` 大小写不敏感，提供 `get` / `has` / `set` / `append` / `delete` / `keys` / `values` / `entries` / `forEach` / `getSetCookie`。
- 非法 URL 与网络失败会以 `TypeError` 拒绝。

```ts
async function main(): Promise<void> {
  const response = await fetch("http://127.0.0.1:8080/status");
  console.log(response.status, await response.text());
}
main();
```

在请求完成前安排的定时器会在交换仍在进行时触发：

```ts
const pending = fetch("http://127.0.0.1:8080/slow");
setTimeout(() => console.log("timer"), 10); // 会在 150ms 的响应之前运行
await pending;
```

已知限制（仅 `http://`、无 TLS、响应体整体缓冲）列在[运行时偏差](../unimplemented/deviations.md)。
