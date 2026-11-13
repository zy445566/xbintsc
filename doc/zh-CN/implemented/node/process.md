# `process` 对象

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/process.md)

实现位置：`src/extensions/node/process/index.ts`、`runtime/ext_node/process/process.c`、
`runtime/ext_node/process/streams.c`

方法调用降为 `xt_process_call(<name>, argc, argv)`，属性读取降为
`xt_process_get(<name>)`，属性写入降为 `xt_process_set(<name>, value)`（见
`src/codegen/generator/tables.ts` 中的 `NAMESPACE_STATICS` /
`NAMESPACE_PROPERTIES` / `NAMESPACE_PROPERTY_SETTERS`）。通过
`import process from "process"` 导入后使用。

| 方法 / 属性 | 说明 |
| --- | --- |
| `process.cwd()` | 当前工作目录 |
| `process.exit([code])` | 先执行 "exit" 监听器，再以 `code` 退出 |
| `process.uptime()` | 进程运行时间（秒） |
| `process.hrtime()` | `[秒, 纳秒]` 数组 |
| `process.getuid()` | 用户 ID（Windows 返回 0） |
| `process.on` / `once` / `addListener` / `prependListener` | 注册事件监听器 |
| `process.off` / `removeListener` / `removeAllListeners` | 移除监听器 |
| `process.emit(event, ...args)` | 手动触发事件 |
| `process.nextTick(fn, ...args)` | 放入微任务队列 |
| `process.exitCode` | 可读可写，作为进程退出码 |
| `process.versions` | `{ node: "0.0.0" }`，没有 `bun` 键（`process.versions.bun` 为 `undefined`） |
| `process.platform` / `process.arch` | 平台 / 架构 |
| `process.pid` / `process.ppid` | 进程 ID / 父进程 ID |
| `process.argv` | 参数数组（`argv[0]` 为可执行文件） |
| `process.env` | 环境变量对象 |
| `process.execPath` | 当前可执行文件路径 |
| `process.version` / `process.title` | 占位版本字符串 / 可写标题 |
| `process.stdin` / `process.stdout` / `process.stderr` | 标准流，见下文 |

> `argv` 由生成的 `main` 通过 `xt_set_program_args` 捕获后提供给运行时。

## 退出与退出码

生成的 `main` 以 `xt_finish()` 结尾：它只执行一次 "exit" 监听器，并返回程序
要求的退出码，因此 `process.exitCode = 1` 与 `process.exit(1)` 都会传给父进程。
`process.exit(code)` 会先执行监听器，所以监听器拿到的参数就是最终退出码。

## 标准流

`process.stdout` / `process.stderr` 是缓存的事件发射器对象，带 `write(text)`、
`end()`、`destroy()`、`fd`、`isTTY`、`columns`、`rows`。`columns`/`rows` 取自
控制台窗口（Windows）或 `TIOCGWINSZ`（POSIX），否则回退到 `COLUMNS`/`LINES`
和 80x24；它们是创建对象时的快照。

`process.stdin` 同样是缓存的事件发射器：

| 成员 | 说明 |
| --- | --- |
| `on("data", fn)` / `once` / `off` | 添加 "data" 监听器会切换到流动模式 |
| `resume()` / `pause()` | 开始 / 停止读取 |
| `setEncoding(name)` | 记录 `encoding`；数据块始终是 UTF-8 字符串 |
| `setRawMode(bool)` | 控制台/终端 raw 模式，见下文 |
| `destroy()` | 停止读取 |
| `isTTY` / `isRaw` / `fd` / `readable` | 流状态 |

阻塞读取运行在异步 I/O 线程池上（Windows 的 `select(2)` 无法监听控制台或管道），
完成后回到主线程把字节解码成字符串，以不超过 4096 字节的块通过 `data` 事件发出。
输入结束先发 `end`，再发 `close`。

raw 模式在 Windows 上打开 `ENABLE_VIRTUAL_TERMINAL_INPUT`，在 POSIX 上使用
`termios` raw 模式，这样 TUI 每次按键就能收到转义序列，而不是按行缓冲的文本。
`atexit` 处理器会恢复原来的模式，因此程序即使没有调用 `setRawMode(false)` 退出，
终端仍然可用。

已知限制：

- 没有 `resize` 事件：`stdout.on("resize", fn)` 能注册但永远不会触发。
- 没有信号事件：`process.on("SIGINT", fn)` 永远不会触发。raw 模式下 Ctrl+C 作为
  输入（`"\u0003"`）送达，这正是 TUI 需要的；行缓冲模式下的程序走系统默认行为。
- 数据块是字符串而不是 `Buffer`，也没有 `read(size)` 拉取接口。
- `process.on` 的监听器挂在发射器对象上；`prependListener` 与 `on` 一样追加，
  触发顺序即注册顺序。
