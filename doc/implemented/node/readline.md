# `readline`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/readline.md)

Location: `src/extensions/node/readline/index.ts`, `runtime/ext_node/readline/readline.c`

`createInterface(options)` (and `new Interface(options)`) reads standard input
line by line. The blocking read runs on the async I/O pool, so the event loop
keeps serving timers while the program waits for a line.

| API | Notes |
| --- | --- |
| `createInterface({ input?, output?, prompt?, terminal? })` | `input`/`output` are informational: the implementation always reads stdin and writes stdout. `prompt` sets the initial prompt. |
| `rl.on(event, fn)` / `rl.once(...)` / `rl.off(...)` / `rl.removeListener(...)` / `rl.removeAllListeners(...)` | `line`, `close`, `pause` and `resume` events |
| `rl.question(query, callback)` | writes `query`, then calls `callback(answer)` with the next line; questions queue in order and do not emit `line` |
| `rl.prompt()` / `rl.setPrompt(text)` / `rl.getPrompt()` | write and manage the prompt (default `"> "`); there is no line editing |
| `rl.pause()` / `rl.resume()` | a line that arrives while paused is buffered and delivered on resume |
| `rl.close()` | emits `close` and stops reading; end of input closes automatically |
| `rl.write(text)` / `rl.setRawMode(bool)` | write to stdout; raw mode is accepted as a no-op |
| `cursorTo(stream, x[, y])`, `moveCursor(stream, dx, dy)`, `clearLine(stream, dir)`, `clearScreenDown(stream)` | write the ANSI escape sequences Node emits; the stream argument is ignored |
| `emitKeypressEvents(stream[, interface])` | accepted as a no-op |

Deviation: there is no terminal line editor, history, completion, `readline/promises`
or keypress synthesis; `input`/`output` custom streams are not redirected.

```ts
import { createInterface } from "readline";

const rl = createInterface({ input: process.stdin, output: process.stdout });
rl.question("name? ", (name: string) => {
  console.log("hello " + name);
  rl.close();
});
```
