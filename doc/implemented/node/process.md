# `process`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/process.md)

Location: `src/extensions/node/process/index.ts`, `runtime/ext_node/process/process.c`

Method calls lower to `xt_process_call(<name>, argc, argv)`, property access to `xt_process_get(<name>)`.
Import the object (`import process from "process"`) to reach these.

| Method / property | Notes |
| --- | --- |
| `process.cwd()` | current working directory |
| `process.exit([code])` | exit the process |
| `process.uptime()` | process uptime (seconds) |
| `process.hrtime()` | `[seconds, nanoseconds]` array |
| `process.getuid()` | user ID (returns 0 on Windows) |
| `process.platform` / `process.arch` | platform / architecture |
| `process.pid` / `process.ppid` | process ID / parent process ID |
| `process.argv` | argument array (`argv[0]` is the executable) |
| `process.env` | environment variable object |
| `process.version` / `process.title` | placeholder strings |

> `argv` is captured by the generated `main` via `xt_set_program_args` and provided to the runtime.
