# `os`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/os.md)

Location: `src/extensions/node/os/index.ts`, `runtime/ext_node/os/os.c`

Uses `os.<name>(...)` namespace calls, lowered to `xt_os_static(<name>, argc, argv)`.
Import the module as a namespace (`import os from "os"`) or pull in individual
functions (`import { platform } from "os"`).

| Method | Notes |
| --- | --- |
| `os.platform()` | `darwin` / `linux` / `win32` / ... |
| `os.arch()` | `x64` / `arm64` / `ia32` / `arm` |
| `os.type()` | `Darwin` / `Linux` / `Windows_NT` / ... |
| `os.release()` | kernel version |
| `os.endianness()` | `LE` / `BE` |
| `os.homedir()` | user home directory |
| `os.tmpdir()` | temporary directory |
| `os.hostname()` | hostname |
| `os.totalmem()` / `os.freemem()` | total / free memory (bytes) |
| `os.cpus()` | array of CPU entries (`model` / `speed` placeholders) |
