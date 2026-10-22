# 完全未实现的模块

> 属于[Node 扩展：未实现](../node.md) · [English](../../../unimplemented/node/modules.md)

以下 Node 内置模块没有任何对应扩展 / 内置函数（`fs` / `fs/promises` / `path` / `os` / `process` / `buffer` / `stream` / `dgram` / `http` / `events` / `util` / `querystring` / `assert` / `test` 已实现，`crypto` / `url` / `child_process` / `zlib` / `stream/promises` / `worker_threads` 部分实现，见[已实现：Node 扩展](../../implemented/node.md)）：

| 模块 | 说明 |
| --- | --- |
| `https` | TLS 版 HTTP |
| `readline` | 命令行读取 |
| `tls` / `cluster` / `vm` | 其余未列出的核心模块 |

> `crypto`（仅 `createHash`，现支持流式 API）、`url`（仅 `pathToFileURL` / `fileURLToPath`）、
> `child_process`（仅 `spawnSync`）、`zlib`（仅 `createGzip`）与
> `worker_threads`（仅 `Worker` / `isMainThread` / `workerData` / `parentPort`）目前为部分实现。
