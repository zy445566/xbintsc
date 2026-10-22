# `process` 对象

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/process.md)

实现位置：`src/extensions/node/process/index.ts`、`runtime/ext_node/process/process.c`

方法调用降为 `xt_process_call(<name>, argc, argv)`，属性访问降为 `xt_process_get(<name>)`。
通过 `import process from "process"` 导入后使用。

| 方法 / 属性 | 说明 |
| --- | --- |
| `process.cwd()` | 当前工作目录 |
| `process.exit([code])` | 退出进程 |
| `process.uptime()` | 进程运行时间（秒） |
| `process.hrtime()` | `[秒, 纳秒]` 数组 |
| `process.getuid()` | 用户 ID（Windows 返回 0） |
| `process.platform` / `process.arch` | 平台 / 架构 |
| `process.pid` / `process.ppid` | 进程 ID / 父进程 ID |
| `process.argv` | 参数数组（`argv[0]` 为可执行文件） |
| `process.env` | 环境变量对象 |
| `process.version` / `process.title` | 占位字符串 |

> `argv` 由生成的 `main` 通过 `xt_set_program_args` 捕获后提供给运行时。
