# `fs/promises` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/fs-promises.md)

实现位置：`src/extensions/node/fs-promises/index.ts`、`runtime/ext_node/fs/async_*.c`、`runtime/ext_node/fs/promises.c`

每个函数都会向运行时的异步 I/O 线程池（`runtime/xt_async_io.c`）提交一个 job：阻塞的系统调用在 worker 线程上执行，promise 在事件循环上 settle。因此 `await` 在大文件读取、慢速网络文件系统或描述符操作进行期间会让出，`then` / `catch` 保持标准 Promise 语义。失败时会以与同步形式相同的 Node `Error`（含 `code` / `errno` / `syscall` / `path`）**reject**。

`fs.promises` 也可从 `fs` 模块访问（`import { promises as fsp } from "fs"`），两个命名空间都暴露 `constants` 对象。

Promise 导出：`readFile`、`writeFile`、`appendFile`、`mkdir`、`readdir`、`rm`、`unlink`、`rmdir`、`rename`、`copyFile`、`cp`、`realpath`、`stat`、`lstat`、`statfs`、`access`、`open`、`chmod`、`lchmod`、`chown`、`lchown`、`truncate`、`utimes`、`lutimes`、`link`、`symlink`、`readlink`、`mkdtemp`、`opendir`、`glob`、`watch`。

`open(...)` 解析为 **`FileHandle`**，带有：`read`、`write`、`readFile`、`writeFile`、`appendFile`、`close`、`stat`、`truncate`、`chmod`、`chown`、`utimes`、`sync`、`datasync`。这些方法同样会 offload；`read(buffer, …)` 会在 worker 完成后填充调用方的 `Buffer`。

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

工作由固定的小型线程池（4 个 worker）执行，完成结果按每个 worker 的提交顺序派发；相互独立的操作可以重叠。任何 job 都不会阻塞其他 `await`、定时器或套接字回调。

已知偏差见[未实现：`fs/promises`](../../unimplemented/node/fs-promises.md)。
