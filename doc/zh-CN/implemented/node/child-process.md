# `child_process` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/child-process.md)

位置：`src/extensions/node/child_process/index.ts`、`runtime/ext_node/child_process/child_process.c`

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

Windows 上子进程通过 `CreateProcess` 启动，stdout/stderr 由读线程经匿名管道持续排空。
不使用临时文件，因此受限的 `%TEMP%` 不会让 `spawnSync` 失败，输出超过管道缓冲区时
也不会死锁。

```ts
import { spawnSync } from "child_process";

const result = spawnSync("clang", ["--version"], { encoding: "utf8" });
console.log(result.status, result.stdout.split("\n")[0]);
```
