# Web Worker

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/web-worker.md)

实现位置：`src/extensions/webworker/index.ts`、`runtime/worker/`

通过 `--ext webworker`（或 `xbintsc.config.json` 里的
`"extensions": ["webworker"]`）启用。它提供浏览器全局对象 `Worker`、
`MessageChannel`、`MessagePort`、`self`、`isMainThread`，以及 worker
脚本里直接使用的 `postMessage` 函数。

## worker 如何运行

xbintsc 没有线程：值模型、垃圾回收器和事件循环都是单线程的。worker 是**第二个
进程**，运行同一个可执行文件。`new Worker(...)` 启动它，两个进程通过回环
（loopback）控制 socket 通信；每条消息在发送端结构化克隆、在接收端重建（数组、
对象、`Map`、`Set`、`Date`、`RegExp`、`ArrayBuffer`、类型化数组、`BigInt`、
`undefined`、共享引用与环）。

由于二进制是提前编译好的，worker 唯一能运行的脚本就是它自己。传入
`import.meta.filename`，并用 `isMainThread` 全局变量分支：

```ts
const worker = new Worker(import.meta.filename, { name: "echo" });
worker.onmessage = (event) => console.log("reply", event.data);

if (!isMainThread) {
  self.onmessage = (event) => {
    self.postMessage(String(event.data).toUpperCase());
    self.close();
  };
}
```

传入其他脚本路径会得到明确的报错，而不是悄悄运行错误的代码。

## API 表面

| API | 说明 |
| --- | --- |
| `new Worker(url, options?)` | `url` 必须是本可执行文件（`import.meta.filename`）；`options.name` 成为 `self.name`；`options.workerData` 不是 Web Worker 选项（请用 Node API） |
| `worker.postMessage(value, transfer?)` | 值做结构化克隆；接受 `transfer` 但按复制处理 |
| `worker.terminate()` | 终止 worker 进程并触发 `exit` |
| `worker.onmessage` / `onmessageerror` / `onerror` | 收到 `MessageEvent` 形状的对象（`type`、`data`、`target`） |
| `worker.addEventListener(type, fn)` / `removeEventListener` | `message`、`error`、`messageerror` |
| `self.postMessage(value)` | worker → 父进程 |
| `self.onmessage` / `addEventListener` | 父进程 → worker |
| `self.close()` | 停止 worker；事件循环排空后进程退出 |
| `self.name` | 父进程传入的 `name` |
| `postMessage(value)` | worker 内 `self.postMessage` 的全局简写 |
| `new MessageChannel()` | `port1`/`port2`，支持 `postMessage`、`onmessage`、`addEventListener`、`close` |

两种约定共享同一份监听器列表，因此 `worker.on("message", fn)`（Node）与
`worker.onmessage = fn` 可以混用：Node 监听器收到原始值，浏览器监听器收到
`MessageEvent`。同样的名字也可以从 `webworker` 模块导入
（`import { Worker, self, isMainThread } from "webworker"`）。

## 生命周期

只要 worker 还在运行，父进程就保持存活。worker 在拥有 message 监听器（或其他
事件循环工作，例如定时器）时保持存活；只 post 一次就结束的 worker 会自动退出。
`self.close()` 或 `worker.terminate()` 可提前结束，父进程随后收到 `exit`。

## 与 GUI 扩展一起使用

`gui` 也定义了 `self` 全局（窗口对象）。`webworker` 注册在 `gui` 之后，所以同时
启用两者时，`self` 是 worker 作用域，页面里请用 `window` 访问窗口。不要依赖页面
中的 `self === window`。

## 限制

- worker 不能运行单独的源文件：所有代码编译进同一个二进制，脚本就是当前程序，
  程序通过 `isMainThread` 分支。
- `MessagePort` 目前不能转移给另一个进程；发送它会抛出
  `MessagePort could not be cloned.`。
- 不支持 `importScripts`、`SharedArrayBuffer` 与真正的并行（worker 是进程，
  值只复制、不共享）。
- worker 内未捕获的异常会打印到 stderr 并以非零码结束该进程；父进程看到的是
  `exit` 事件，而不是 `error`。
