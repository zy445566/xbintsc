# `fs/promises`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/fs-promises.md)

Location: `src/extensions/node/fs-promises/index.ts`, `runtime/ext_node/fs/async_*.c`,
`runtime/ext_node/fs/promises.c`.

Every function submits a job to the runtime's asynchronous I/O pool
(`runtime/xt_async_io.c`): the blocking syscall runs on a worker thread and the
promise settles on the event loop. `await` therefore yields while a large read,
a slow network filesystem or a descriptor operation is in progress, and `then` /
`catch` keep the usual Promise semantics. Failures **reject** with the same
Node-shaped `Error` (with `code` / `errno` / `syscall` / `path`) that the
synchronous form throws.

`fs.promises` is also reachable from the `fs` module
(`import { promises as fsp } from "fs"`), and both namespaces expose a
`constants` object.

Promise exports: `readFile`, `writeFile`, `appendFile`, `mkdir`, `readdir`,
`rm`, `unlink`, `rmdir`, `rename`, `copyFile`, `cp`, `realpath`, `stat`,
`lstat`, `statfs`, `access`, `open`, `chmod`, `lchmod`, `chown`, `lchown`,
`truncate`, `utimes`, `lutimes`, `link`, `symlink`, `readlink`, `mkdtemp`,
`opendir`, `glob`, `watch`.

`open(...)` resolves to a **`FileHandle`** with:
`read`, `write`, `readFile`, `writeFile`, `appendFile`, `close`, `stat`,
`truncate`, `chmod`, `chown`, `utimes`, `sync`, `datasync`. Those methods are
offloaded too; `read(buffer, …)` fills the caller's `Buffer` when the worker
completes.

```ts
import { readFile, writeFile, open } from "fs/promises";

async function main(): Promise<void> {
  await writeFile("/tmp/a.txt", "hi");
  console.log(await readFile("/tmp/a.txt"));
  const handle = await open("/tmp/a.txt", "r");
  console.log(await handle.readFile("utf8"));
  await handle.close();
}
main();
```

Work runs on a small fixed pool (four workers) and completions are dispatched in
submission order per worker; independent operations can overlap. A job never
blocks another `await`, a timer or a socket callback.

Known deviations are listed in [Unimplemented: `fs/promises`](../../unimplemented/node/fs-promises.md).
