# `child_process` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/child-process.md)

位置：`src/extensions/node/child_process/index.ts`、`runtime/ext_node/child_process/child_process.c`

`spawnSync(command, args[, options])` 运行一个程序直到结束，返回
`{ status, stdout, stderr }`。`options.cwd` 设置子进程工作目录；
`options.stdio: "inherit"` 让子进程共用父进程的 stdout/stderr（`xbintsc run`
用它把被编译程序的输出实时透传），否则 stdout/stderr 会作为 UTF-8 字符串捕获。

| 选项 | 说明 |
| --- | --- |
| `cwd` | 子进程的工作目录 |
| `stdio: "inherit"` | 共用父进程的 stdout/stderr，而不是捕获 |
| `encoding` | 接受但忽略（输出始终按 UTF-8 解码） |

Windows 上子进程通过 `CreateProcess` 启动，stdout/stderr 由读线程经匿名管道持续排空。
不使用临时文件，因此受限的 `%TEMP%` 不会让 `spawnSync` 失败，输出超过管道缓冲区时
也不会死锁。

```ts
import { spawnSync } from "child_process";

const result = spawnSync("clang", ["--version"], { encoding: "utf8" });
console.log(result.status, result.stdout.split("\n")[0]);
```
