# `readline`

> 属于 [Node 扩展：已实现](../node.md) · [English](../../../implemented/node/readline.md)

位置：`src/extensions/node/readline/index.ts`、`runtime/ext_node/readline/readline.c`

`createInterface(options)`（以及 `new Interface(options)`）按行读取标准输入。
阻塞读取运行在异步 I/O 线程池上，因此程序等待输入时事件循环仍会继续处理定时器。

| API | 说明 |
| --- | --- |
| `createInterface({ input?, output?, prompt?, terminal? })` | `input`/`output` 仅作声明：实现始终读 stdin、写 stdout。`prompt` 设置初始提示符。 |
| `rl.on(event, fn)` / `rl.once(...)` / `rl.off(...)` / `rl.removeListener(...)` / `rl.removeAllListeners(...)` | 支持 `line`、`close`、`pause`、`resume` 事件 |
| `rl.question(query, callback)` | 写出 `query`，用下一行调用 `callback(answer)`；问题按顺序排队，且不触发 `line` |
| `rl.prompt()` / `rl.setPrompt(text)` / `rl.getPrompt()` | 写出并管理提示符（默认 `"> "`）；没有行编辑 |
| `rl.pause()` / `rl.resume()` | 暂停期间到达的行会缓存，恢复后投递 |
| `rl.close()` | 触发 `close` 并停止读取；输入结束会自动关闭 |
| `rl.write(text)` / `rl.setRawMode(bool)` | 写到 stdout；raw mode 作为无操作接受 |
| `cursorTo(stream, x[, y])`、`moveCursor(stream, dx, dy)`、`clearLine(stream, dir)`、`clearScreenDown(stream)` | 写出 Node 相同的 ANSI 转义序列；stream 参数被忽略 |
| `emitKeypressEvents(stream[, interface])` | 作为无操作接受 |

差异：没有终端行编辑、历史、补全、`readline/promises` 或按键事件合成；
不会重定向自定义 `input`/`output` 流。

```ts
import { createInterface } from "readline";

const rl = createInterface({ input: process.stdin, output: process.stdout });
rl.question("name? ", (name: string) => {
  console.log("hello " + name);
  rl.close();
});
```
