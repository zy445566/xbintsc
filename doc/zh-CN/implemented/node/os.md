# `os` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/os.md)

实现位置：`src/extensions/node/os/index.ts`、`runtime/ext_node/os/os.c`

采用 `os.<name>(...)` 命名空间调用，降为 `xt_os_static(<name>, argc, argv)`。
以命名空间方式导入（`import os from "os"`），或单独导入函数（`import { platform } from "os"`）。

| 方法 | 说明 |
| --- | --- |
| `os.platform()` | `darwin` / `linux` / `win32` / ... |
| `os.arch()` | `x64` / `arm64` / `ia32` / `arm` |
| `os.type()` | `Darwin` / `Linux` / `Windows_NT` / ... |
| `os.release()` | 内核版本 |
| `os.endianness()` | `LE` / `BE` |
| `os.homedir()` | 用户主目录 |
| `os.tmpdir()` | 临时目录 |
| `os.hostname()` | 主机名 |
| `os.totalmem()` / `os.freemem()` | 总内存 / 可用内存（字节） |
| `os.cpus()` | CPU 条目数组（`model` / `speed` 占位） |
