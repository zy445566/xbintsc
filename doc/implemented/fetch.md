# fetch

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/fetch.md)

Location: `runtime/xt_fetch.c`.

- Global `fetch(input, init)` is a blocking HTTP/1.1 client whose promise is already settled (the exchange happens synchronously; it is not scheduled on the reactor).
- It accepts `http://` URLs only — `https://` rejects with a `TypeError` because no TLS backend is linked.
- Options: `method`, `headers` (plain object or `Headers`), `body` and `redirect` (`follow` / `manual` / `error`); up to 20 redirects are followed, and a 301/302/303 switches POST to GET.
- The resolved `Response`-shaped object exposes `ok` / `status` / `statusText` / `url` / `headers` plus `text()` / `json()` / `arrayBuffer()` / `bytes()` / `clone()`.
- `Headers` is case-insensitive with `get` / `has` / `set` / `append` / `delete` / `keys` / `values` / `entries` / `forEach` / `getSetCookie`.
- Invalid URLs and network failures reject with a `TypeError`.
