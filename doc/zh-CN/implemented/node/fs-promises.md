# `fs/promises` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/fs-promises.md)

实现位置：`src/extensions/node/fs-promises/index.ts`、`runtime/ext_node/fs/promises.c`

无异步 I/O 调度器，故每个函数把对应的同步 `fs` 实现包进**已 settle 的 Promise**。失败时会以同步形式抛出的同构 Node `Error`（含 `code` / `errno` / `syscall` / `path`）**reject**。`fs.promises` 也可从 `fs` 模块访问（`import { promises as fsp } from "fs"`），两个命名空间都暴露 `constants` 对象。

Promise 导出：`readFile`、`writeFile`、`appendFile`、`mkdir`、`readdir`、`rm`、`unlink`、`rmdir`、`rename`、`copyFile`、`cp`、`realpath`、`stat`、`lstat`、`statfs`、`access`、`open`、`chmod`、`lchmod`、`chown`、`lchown`、`truncate`、`utimes`、`lutimes`、`link`、`symlink`、`readlink`、`mkdtemp`、`opendir`、`glob`、`watch`。

`open(...)` 解析为 **`FileHandle`**，带有：`read`、`write`、`readFile`、`writeFile`、`appendFile`、`close`、`stat`、`truncate`、`chmod`、`chown`、`utimes`、`sync`、`datasync`。

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


已知偏差见[未实现：`fs/promises`](../../unimplemented/node/fs-promises.md)。
