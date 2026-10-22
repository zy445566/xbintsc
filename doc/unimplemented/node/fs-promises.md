# `fs/promises` gaps

> Part of [Node extension: unimplemented](../node.md) · [简体中文](../../zh-CN/unimplemented/node/fs-promises.md)

`fs/promises` wraps each synchronous `fs` call in an already-settled promise
(the implemented surface is in
[implemented: `fs/promises`](../../implemented/node/fs-promises.md)).

- I/O is synchronous: the promise is settled before it is awaited, so awaiting
  it does not yield to the event loop.
- `FileHandle.appendFile` behaves like `writeFile` (it writes at the current
  file position instead of appending).
- `FileHandle.readFile()` reads from the current file-descriptor offset.
- `Dir.read` / `Dir.close` (sync and promise forms) complete immediately.
- `watch` resolves to the same never-emitting watcher as `fs.watch`; there is no
  real file-watching backend.
