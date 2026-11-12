# Fully unimplemented modules

> Part of [Node extension: unimplemented](../node.md) · [简体中文](../../zh-CN/unimplemented/node/modules.md)

The following Node built-in modules have no corresponding extension / builtin
(`fs` / `fs/promises` / `path` / `os` / `process` / `buffer` / `stream` /
`dgram` / `http` / `tls` / `http2` / `events` / `util` / `querystring` /
`readline` / `assert` / `test` are implemented, and `crypto` / `url` /
`child_process` / `zlib` / `stream/promises` / `worker_threads` are partially
implemented; see [implemented: Node extension](../../implemented/node.md)):

| Module | Notes |
| --- | --- |
| `https` | TLS version of HTTP; use `http2` or `tls` directly, or `fetch` for `https://` |
| `cluster` / `vm` | other core modules not listed |

> `crypto` (only `createHash`, now with the streaming API), `url` (only
> `pathToFileURL` / `fileURLToPath`), `child_process` (only `spawnSync` /
> `spawn` / `execFile` / `execSync`), `zlib` (only `createGzip`) and
> `worker_threads` (only `Worker` / `isMainThread` / `workerData` /
> `parentPort`) are partially implemented.
