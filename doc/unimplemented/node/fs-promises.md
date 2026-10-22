# `fs/promises` gaps

> Part of [Node extension: unimplemented](../node.md) · [简体中文](../../zh-CN/unimplemented/node/fs-promises.md)

`fs/promises` now offloads its blocking I/O to the async I/O pool (the
implemented surface is in
[implemented: `fs/promises`](../../implemented/node/fs-promises.md)); the
remaining differences are behavioural:

- `FileHandle.appendFile` behaves like `writeFile` (it writes at the current
  file position instead of appending).
- `FileHandle.readFile()` reads from the current file-descriptor offset.
- `Dir.read` / `Dir.close` (sync and promise forms) complete their callbacks
  immediately; `Dir.readSync` / `closeSync` remain synchronous.
- `watch` resolves to the same never-emitting watcher as `fs.watch`; there is no
  real file-watching backend.
- Reads and writes are whole-file/one-shot operations; there are no streaming
  `FileHandle` variants and no `AbortSignal` cancellation.
- The pool has four workers; very many simultaneous operations queue behind
  them (they still do not block the event loop).
