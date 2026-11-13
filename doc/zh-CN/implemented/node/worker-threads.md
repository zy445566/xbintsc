# `worker_threads` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/worker-threads.md)

实现位置：`src/extensions/node/worker_threads/index.ts`、`runtime/worker/`

xbintsc 没有线程（值模型、垃圾回收器与事件循环都是单线程的），所以 worker 是运行
同一个可执行文件的**第二个进程**。worker 入口就是当前程序：传入
`import.meta.filename`，并用 `isMainThread` 分支。

```ts
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

if (isMainThread) {
  const worker = new Worker(import.meta.filename, { workerData: { n: 2 } });
  worker.on("message", (reply) => {
    console.log(reply.total);
    worker.terminate();
  });
  worker.postMessage({ values: [1, 2, 3] });
} else {
  parentPort.on("message", (message) => {
    const values: number[] = message.values;
    parentPort.postMessage({ total: values[0] + values[1] + values[2] });
  });
}
```

## API 表面

- `Worker(script, options?)` —— `options.workerData`（结构化克隆）与
  `options.name`；`script` 必须是本可执行文件，否则构造器抛出明确的错误。
  `options.env`、`argv`、`execArgv`、`resourceLimits`、
  `stdin`/`stdout`/`stderr` 接受但忽略。
- `worker.postMessage(value, transferList?)` —— 值做结构化克隆；接受 transfer
  列表但按复制处理。发送函数、symbol、promise、`WeakMap`/`WeakSet`/`WeakRef`、
  生成器或 `MessagePort` 会抛出 `... could not be cloned.`。
- `worker.on("message", ...)` / `worker.once` / `off` / `removeAllListeners` ——
  `message` 传递原始值。
- `worker.on("online" | "error" | "exit", ...)` —— 控制通道建立后触发
  `online`；`exit` 携带进程退出码（被终止的 worker 报 `1`）。帧解码失败时触发
  `messageerror`。
- `worker.terminate(): Promise<number>` —— 终止进程，resolve 退出码；
  `ref()`/`unref()` 接受但为空操作。
- `worker.threadId` —— 每个进程的 worker id。
- `isMainThread`、`workerData`（主线程为 null）、`parentPort`（主线程为 null），
  后者支持 `postMessage`、`on("message")`、`close()`、`start()`、
  `ref()`/`unref()`。
- `new MessageChannel()` / `MessagePort` —— 进程内通道：`port1.postMessage`
  通过零延时定时器投递给 `port2`；两个端口都支持 `on("message")`（原始值）与
  `onmessage` / `addEventListener`（`MessageEvent`）。`receiveMessageOnPort`
  存在，但本地端口本来就通过定时器投递，因此返回 `undefined`。
- `markAsUncloneable(value)` —— 接受；编解码器本来就拒绝流与宿主句柄。

## 消息数据

消息与 `workerData` 使用 `runtime/worker/worker_codec.c` 的结构化克隆编解码器：
`undefined`、布尔、数字（含 `NaN`、`±Infinity`、`-0`）、字符串、`BigInt`、
数组、普通对象、`Map`、`Set`、`Date`、`RegExp`、`ArrayBuffer`、类型化数组、
共享引用与环。类实例按普通对象到达；函数与 symbol 会被拒绝。

## 生命周期与错误

worker 运行期间父进程保持存活。worker 拥有 `message` 监听器（或其他事件循环
工作）时保持存活；只 post 一次就返回的 worker 会自行退出，所以长驻 worker 的
常见模式是 `worker.on("message")` 加 `terminate()`。worker 返回时会在进程结束前
关闭控制通道，因此退出前 post 的消息仍会送达。worker 内未捕获的异常由该
进程打印到 stderr 并非零退出；父进程看到的是 `exit`，而不是 `error`。父进程最后
一个句柄关闭时，worker 会因 socket EOF 而结束。

## 平台差异

传输层是回环 TCP socket（Windows 的 `select(2)` 只接受 socket，管道需要另一套
实现）。POSIX 使用 `fork`/`execvp` 与 `waitpid(WNOHANG)`；Windows 使用
`_spawnvp(_P_NOWAIT)` 与进程句柄上的 `WaitForSingleObject`。端口与随机握手
token 通过环境变量传递，因此 Windows 上 `workerData` 上限为 32 KiB。

## 限制

跨进程 `MessagePort` 转移、`BroadcastChannel`、
`setEnvironmentData`/`getEnvironmentData`、`SHARE_ENV` 与真正的线程并行尚未
实现——见 [Node 扩展：未实现](../../unimplemented/node.md)。
