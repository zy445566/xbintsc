# Node extension: unimplemented

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/node.md)

The Node extension implements 21 modules plus the event-loop integration; the
implemented surface is documented page by page under
[Node extension: implemented](../implemented/node.md). This section records what
is missing or differs from Node.

## Contents

- [`fs`](./node/fs.md) — callback-style async operations, real file watching, option callbacks
- [`fs/promises`](./node/fs-promises.md) — synchronous I/O behind a settled promise, `FileHandle` deviations
- [Fully unimplemented modules](./node/modules.md) — `https`, `readline`, `tls`, `cluster`, `vm`, ...
- [Global objects and namespaces](./node/globals.md) — `global`, `__dirname`, `require` outside `node_modules`
- [Async and the event-loop model](./node/async.md) — no asynchronous I/O scheduler, blocking `connect`, no keep-alive
- [The Bun extension](./node/bun.md) — design example only

A module's implemented page links to its limitation page; when a limitation is
lifted, move the entry to the matching page under
`../implemented/node/`.
