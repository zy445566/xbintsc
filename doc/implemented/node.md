# Node extension: implemented

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/node.md)

Location: `src/extensions/node/`, `runtime/ext_node/`

The `node` extension is optional: enable it with `--ext node` (or
`"extensions": ["node"]` in `xbintsc.config.json`). It is a platform extension:
one subdirectory per Node module, pairing the TypeScript exports with the C
implementation. Adding a module does not change the core compiler; the extension
contract is described in [Extension mechanism](./extensions.md).

Every module has its own page so a change to one module does not collide with
work on another. Missing or different behaviour is recorded under
[Node extension: unimplemented](../unimplemented/node.md).

## Modules

| Module | Page | Notes |
| --- | --- | --- |
| `fs` | [fs](./node/fs.md) | synchronous API only |
| `fs/promises` | [fs/promises](./node/fs-promises.md) | reactor-driven file and `FileHandle` operations on the async I/O pool |
| `path` | [path](./node/path.md) | `path.<name>(...)` namespace calls |
| `os` | [os](./node/os.md) | `os.<name>(...)` namespace calls |
| `process` | [process](./node/process.md) | object: methods and properties |
| `buffer` | [buffer](./node/buffer.md) | `Buffer` represented as a plain object |
| `stream` | [stream](./node/stream.md) | `Readable` / `Writable` / `Duplex` / `Transform` / `PassThrough` |
| `stream/promises` | [stream/promises](./node/stream-promises.md) | `pipeline` only |
| `net` | [net](./node/net.md) | TCP server and client |
| `dgram` | [dgram](./node/dgram.md) | UDP sockets |
| `http` | [http](./node/http.md) | server and client, `Connection: close` |
| `child_process` | [child_process](./node/child-process.md) | `spawnSync` only |
| `events` | [events](./node/events.md) | `EventEmitter` (global and named export) |
| `util` | [util](./node/util.md) | formatting, inspection and predicates |
| `querystring` | [querystring](./node/querystring.md) | parse / stringify / escape |
| `assert` | [assert](./node/assert.md) | assertion functions |
| `test` | [test](./node/test.md) | minimal TAP runner |
| `crypto` | [crypto](./node/crypto.md) | `createHash` (SHA-1 / SHA-256) |
| `url` | [url](./node/url.md) | `pathToFileURL` / `fileURLToPath` |
| `zlib` | [zlib](./node/zlib.md) | `createGzip` only |
| `worker_threads` | [worker_threads](./node/worker-threads.md) | emulated with child processes |

## Where does a change belong?

A Node module change normally touches `src/extensions/node/<module>/` and
`runtime/ext_node/<module>/`; document it in `node/<module>.md` and, if it
changes a limitation, in the matching page under
`../../unimplemented/node/`. The compiler core does not change.
