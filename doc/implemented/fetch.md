# fetch

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/fetch.md)

Location: `runtime/xt_fetch.c`, `runtime/xt_fetch/async.inc`, `runtime/xt_async_io.c`.

- Global `fetch(input, init)` is an HTTP/1.1 client. The blocking socket exchange
  (`getaddrinfo`, connect, send, receive) runs on the asynchronous I/O worker
  pool, so a request in flight does **not** stall the event loop: timers, sockets
  and other `await`s keep running and the promise settles when the reactor
  dispatches the completed exchange.
- Redirects are followed on the main thread: each hop is one offloaded
  exchange, up to 20 redirects, and a 301/302/303 switches POST to GET.
- It accepts `http://` URLs only — `https://` rejects with a `TypeError` because
  no TLS backend is linked.
- A reference with **no scheme** (a relative path) and `file://` URLs read a
  local file, so a compiled program can load an asset from disk without an HTTP
  server. Relative paths resolve against the working directory; `GET` / `HEAD`
  are supported.
- Options: `method`, `headers` (plain object or `Headers`), `body` and `redirect`
  (`follow` / `manual` / `error`).
- The resolved `Response`-shaped object exposes `ok` / `status` / `statusText` /
  `url` / `headers` plus `text()` / `json()` / `arrayBuffer()` / `bytes()` /
  `clone()`. The body is buffered before the promise settles; streaming bodies
  are not implemented.
- `Headers` is case-insensitive with `get` / `has` / `set` / `append` /
  `delete` / `keys` / `values` / `entries` / `forEach` / `getSetCookie`.
- Invalid URLs and network failures reject with a `TypeError`.

```ts
async function main(): Promise<void> {
  const response = await fetch("http://127.0.0.1:8080/status");
  console.log(response.status, await response.text());
}
main();
```

A timer scheduled before the request completes fires while the exchange is still
in flight:

```ts
const pending = fetch("http://127.0.0.1:8080/slow");
setTimeout(() => console.log("timer"), 10); // runs before the 150ms response
await pending;
```

Known limits (only `http://`, no TLS, buffered body) are listed in
[Runtime deviations](../unimplemented/deviations.md).
