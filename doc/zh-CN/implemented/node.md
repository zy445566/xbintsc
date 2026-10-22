# Node 扩展：已实现

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/node.md)

实现位置：`src/extensions/node/`、`runtime/ext_node/`

`node` 扩展是可选的：通过 `--ext node`（或 `xbintsc.config.json` 里的
`"extensions": ["node"]`）启用。它是平台扩展：每个 Node 模块一个子目录，
TypeScript 导出与 C 实现一一对应。新增模块不需要改动核心编译器；扩展契约见
[扩展机制](./extensions.md)。

每个模块有独立页面，改动一个模块不会和另一个模块的工作冲突。缺失或行为不同之处
记录在[Node 扩展：未实现](../../unimplemented/node.md)。

## 模块

| 模块 | 页面 | 说明 |
| --- | --- | --- |
| `fs` | [fs](./node/fs.md) | 仅同步 API |
| `fs/promises` | [fs/promises](./node/fs-promises.md) | 由 reactor 驱动、跑在异步 I/O 线程池上的文件与 `FileHandle` 操作 |
| `path` | [path](./node/path.md) | `path.<name>(...)` 命名空间调用 |
| `os` | [os](./node/os.md) | `os.<name>(...)` 命名空间调用 |
| `process` | [process](./node/process.md) | 对象：方法与属性 |
| `buffer` | [buffer](./node/buffer.md) | `Buffer` 以普通对象表示 |
| `stream` | [stream](./node/stream.md) | `Readable` / `Writable` / `Duplex` / `Transform` / `PassThrough` |
| `stream/promises` | [stream/promises](./node/stream-promises.md) | 仅 `pipeline` |
| `net` | [net](./node/net.md) | TCP 服务端与客户端 |
| `dgram` | [dgram](./node/dgram.md) | UDP 套接字 |
| `http` | [http](./node/http.md) | 服务端与客户端，`Connection: close` |
| `child_process` | [child_process](./node/child-process.md) | 仅 `spawnSync` |
| `events` | [events](./node/events.md) | `EventEmitter`（全局与具名导出） |
| `util` | [util](./node/util.md) | 格式化、inspect 与类型谓词 |
| `querystring` | [querystring](./node/querystring.md) | parse / stringify / escape |
| `assert` | [assert](./node/assert.md) | 断言函数 |
| `test` | [test](./node/test.md) | 最小 TAP 运行器 |
| `crypto` | [crypto](./node/crypto.md) | `createHash`（SHA-1 / SHA-256） |
| `url` | [url](./node/url.md) | `pathToFileURL` / `fileURLToPath` |
| `zlib` | [zlib](./node/zlib.md) | 仅 `createGzip` |
| `worker_threads` | [worker_threads](./node/worker-threads.md) | 以子进程模拟 |

## 改动应该写在哪一页？

Node 模块改动通常涉及 `src/extensions/node/<module>/` 与 `runtime/ext_node/<module>/`；
文档写到 `node/<module>.md`，若改变了限制，再写到
`../../unimplemented/node/` 下对应页面。核心编译器不需要改动。
