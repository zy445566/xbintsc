# `fs`

> Part of [Node extension: unimplemented](../node.md) · [简体中文](../../zh-CN/unimplemented/node/fs.md)

`fs` now implements the **synchronous** API surface (files, directories,
descriptors, metadata, links, `cp`, `glob`, `Dir`, `constants`) and
`fs/promises` wraps each synchronous function in an already-settled Promise
(see [implemented: `fs`](../../implemented/node/fs.md) and
[implemented: `fs/promises`](../../implemented/node/fs-promises.md)). The rest is still
unimplemented because xbintsc has **no asynchronous I/O scheduler** for file
operations (the `fs` module has no reactor backend; the core event loop does
provide timers and sockets):

| Unimplemented API | Category |
| --- | --- |
| `readFile` / `writeFile` / `appendFile` / `open` / `close` / `read` / `write` / `stat` / … | callback-style async file operations (`fs/promises` provides the Promise forms) |
| `createReadStream` / `createWriteStream` | streaming read/write |
| `watch` / `watchFile` | **real** file watching: the functions exist but the returned objects never emit (there is no inotify/kqueue backend) |
| `openAsBlob`, `statfs` callback form, `rmdir` `maxRetries` / `retryDelay` | misc. options requiring async retry |
| `glob` `exclude` / `follow`, `cp` `filter` | option callbacks |

## Read semantics gaps

- `readFileSync` supports encoding options (default/`utf8`/`ascii`/`latin1`/`binary`/ `hex`/`base64`/`base64url`), but **does not return a `Buffer`** by default: even though `Buffer` exists (simulated as a plain object), `readFileSync` still returns a string. Pass a `Buffer`-producing path or read raw bytes explicitly.
- `utf16le` / `ucs2` decoding is not implemented (the bytes are returned as text).
- `watch` / `watchFile` / `unwatchFile` return API-shaped emitter objects whose `.close()` / `.on()` methods exist but which **never fire** events.
- `Dir.read(cb)` / `Dir.close(cb)` and the async form of `FileHandle` methods complete **synchronously** (the callback/Promise is invoked immediately).
- `mkdtempSync` always appends 6 random characters to the prefix (it does not require a trailing `XXXXXX`).
- `globSync` supports `*`, `?`, `[...]` and `**` but not the `exclude` callback or `follow`; `**` does not descend through symlinks (matching Node's default).
- `cpSync` symlink handling follows Node for the common cases but `verbatimSymlinks` is not supported.
- Platform deviations: on Windows `readlinkSync` raises `ENOSYS` and `chmodSync` / `lchmodSync` / `chownSync` / `lchownSync` / `fchmodSync` / `fchownSync` are no-ops; `statfsSync` returns zeroed fields on Windows/AIX/Sun; `lutimesSync` falls back to `utimesSync` on macOS/Windows.
