# `process`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/process.md)

Location: `src/extensions/node/process/index.ts`, `runtime/ext_node/process/process.c`,
`runtime/ext_node/process/streams.c`

Method calls lower to `xt_process_call(<name>, argc, argv)`, property reads to
`xt_process_get(<name>)` and property writes to `xt_process_set(<name>, value)`
(see `NAMESPACE_STATICS` / `NAMESPACE_PROPERTIES` / `NAMESPACE_PROPERTY_SETTERS`
in `src/codegen/generator/tables.ts`). Import the object
(`import process from "process"`) to reach these.

| Method / property | Notes |
| --- | --- |
| `process.cwd()` | current working directory |
| `process.exit([code])` | run the "exit" listeners, then exit with `code` |
| `process.uptime()` | process uptime (seconds) |
| `process.hrtime()` | `[seconds, nanoseconds]` array |
| `process.getuid()` | user ID (returns 0 on Windows) |
| `process.on` / `once` / `addListener` / `prependListener` | register an event listener |
| `process.off` / `removeListener` / `removeAllListeners` | remove listeners |
| `process.emit(event, ...args)` | fire an event by hand |
| `process.nextTick(fn, ...args)` | queue on the microtask queue |
| `process.exitCode` | read **and** write; becomes the program's exit status |
| `process.versions` | `{ node: "0.0.0" }` — no `bun` key, so `process.versions.bun` is `undefined` |
| `process.platform` / `process.arch` | platform / architecture |
| `process.pid` / `process.ppid` | process ID / parent process ID |
| `process.argv` | argument array (`argv[0]` is the executable) |
| `process.env` | environment variable object |
| `process.execPath` | path of the running binary |
| `process.version` / `process.title` | placeholder version string / writable title |
| `process.stdin` / `process.stdout` / `process.stderr` | standard streams, see below |

> `argv` is captured by the generated `main` via `xt_set_program_args` and provided to the runtime.

## Termination and the exit status

The generated `main` ends with `xt_finish()`: it runs the "exit" listeners once
and returns the status the program asked for, so `process.exitCode = 1` and
`process.exit(1)` both reach the parent process. `process.exit(code)` runs the
listeners before leaving, which means a listener sees the final code as its
argument.

## Standard streams

`process.stdout` and `process.stderr` are cached EventEmitter objects with
`write(text)`, `end()`, `destroy()`, `fd`, `isTTY`, `columns` and `rows`.
`columns`/`rows` come from the console window (Windows) or `TIOCGWINSZ` (POSIX),
falling back to `COLUMNS`/`LINES` and then 80x24; they are read once when the
object is created.

`process.stdin` is a cached EventEmitter:

| Member | Notes |
| --- | --- |
| `on("data", fn)` / `once` / `off` | attaching a "data" listener switches the stream to flowing mode |
| `resume()` / `pause()` | start or stop the read loop |
| `setEncoding(name)` | records `encoding`; chunks are always UTF-8 strings |
| `setRawMode(bool)` | console/terminal raw mode, see below |
| `destroy()` | stops reading |
| `isTTY` / `isRaw` / `fd` / `readable` | stream state |

A blocking read runs on the async I/O worker pool (the reactor's `select(2)`
cannot watch a console or a pipe on Windows), and its completion — which runs on
the main thread — decodes the bytes and emits `data` with a string chunk of at
most 4096 bytes. End of input emits `end` and then `close`.

Raw mode flips the console into `ENABLE_VIRTUAL_TERMINAL_INPUT` (Windows) or a
`termios` raw mode (POSIX) so a TUI receives escape sequences per keystroke
instead of line-buffered text. An `atexit` handler restores the previous mode, so
a program that exits without calling `setRawMode(false)` leaves the terminal
usable.

Known limits:

- No `resize` events: `stdout.on("resize", fn)` registers but never fires.
- No signal events: `process.on("SIGINT", fn)` never fires. In raw mode the
  terminal delivers Ctrl+C as input (`"\u0003"`), which is what a TUI wants; a
  line-buffered program terminates through the OS default.
- Chunks are strings, not `Buffer`s, and there is no `read(size)` pull API.
- `process.on` listeners are stored on the emitter object; `prependListener`
  appends like `on` (order is registration order).
