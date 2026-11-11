# `child_process` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/child-process.md)

位置：`src/extensions/node/child_process/index.ts`、`runtime/ext_node/child_process/`

`spawnSync(command, args[, options])` 运行一个程序直到结束，返回
`{ status, stdout, stderr }`。`options.cwd` 设置子进程工作目录，`options.env`
替换子进程的环境变量，`options.stdio: "inherit"` 让子进程共用父进程的
stdout/stderr（`xbintsc run` 用它把被编译程序的输出实时透传），否则
stdout/stderr 会作为 UTF-8 字符串捕获。

| 选项 | 说明 |
| --- | --- |
| `cwd` | 子进程的工作目录 |
| `env` | 子进程的完整环境变量；不传则继承父进程，想扩展就展开 `process.env` |
| `stdio: "inherit"` | 共用父进程的 stdout/stderr，而不是捕获 |
| `encoding` | 接受但忽略（输出始终按 UTF-8 解码） |

`env` 遵循 Node 的替换语义：传了对象，它就是子进程的完整环境，调用方没列出的
变量在子进程里不存在。这正是调用方能把特定的 `SDKROOT`、`INCLUDE`、`LIB` 交给
工具链、又不泄漏自身 shell 环境的原因；自举出来的编译器跑的就是这段代码，macOS
的 SDK 修复也依赖它。

异步形式也已实现：

- `spawn(command, args[, options])` 返回一个 `ChildProcess`，其
  `stdout`/`stderr` 是 `Readable` 流，并会发出 `exit`/`close`/`error` 事件；
- `execFile(file, args[, options], callback)` 不经 shell 直接运行，回调
  `callback(error, stdout)`；
- `execSync(command[, options])` 通过平台 shell（Windows 为 `cmd /c`，其余为
  `/bin/sh -c`）运行并返回捕获到的 stdout 字符串。

它们接受与 `spawnSync` 相同的 `cwd`/`env` 选项；`stdio` 额外支持
`"ignore"`（丢弃子进程的 stdout/stderr）。捕获在异步 I/O 线程池上执行，因此
挂起的 `spawn`/`execFile` 不会阻塞 reactor。

同步路径在 `child_process.c` 中；异步 worker 按平台拆分（`spawn_win.c` /
`spawn_posix.c`，由扩展的 `runtimeSources()` 选择）。Windows 上子进程通过
`CreateProcess` 启动，stdout/stderr 由读线程经匿名管道排空；POSIX 上通过
`fork` + `execvp` 启动，管道由 `poll(2)` 排空。不使用临时文件，因此受限的
`%TEMP%` 不会让 `spawnSync` 失败，输出超过管道缓冲区时也不会死锁。

```ts
import { spawnSync, spawn, execFile, execSync } from "child_process";

const result = spawnSync("clang", ["--version"], { encoding: "utf8" });
console.log(result.status, result.stdout.split("\n")[0]);

const child = spawn("clang", ["--version"]);
child.stdout.on("data", (chunk: string) => console.log(chunk));

execFile("clang", ["--version"], (error: unknown, stdout: string) => console.log(stdout));

console.log(execSync("clang --version"));
```
