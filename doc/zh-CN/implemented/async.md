# 异步、Promise 与生成器

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/async.md)

实现位置：`runtime/xt_async.c`、`runtime/xt_coroutine.c`、`runtime/xt_promise.c`、
`runtime/xt_generator.c`、`runtime/xt_loop.c`、`runtime/xt_async_io.c`、
`runtime/xt_performance.c`。

## async / await

- `async` 闭包用 `xt_function_set_async` 标记；调用时在栈式协程（`xt_coroutine.c`、`xt_async.c`）上运行函数体并立即返回 promise。
- 函数体同步执行到第一个等待**未 settle** promise 的 `await` 后挂起；promise settle 时排入微任务恢复协程。
- 逃出函数体的异常会让该 promise reject，而不是同步抛出。
- `await` 非 promise 或已 settle 的值仍走同步快路径，rejection 触发异常。
- 每次 `async` 调用拥有私有栈，切换异常帧栈与 GC 栈区间，因此 `await` 可跨越 `try`/`finally`、循环与闭包。由定时器、套接字回调或另一个协程 settle 的 promise 会在微任务队列上恢复挂起的函数体。

## 异步生成器

- `async function*` 闭包用 `xt_function_set_async_generator` 标记；每次 `.next()` / `.throw()` / `.return()` 入队一个请求并返回 promise。
- 函数体在生成器协程上运行，可在 `yield` 或 `await` 处挂起；`xt_generator_await` 将协程挂在被等待的 promise 上，并让当前请求保持 pending，直到下一次 yield 或函数体结束。
- `for await...of` 降级为 `xt_async_iter_open` / `xt_async_iter_next` + `xt_await`，因此同时支持异步可迭代对象与同步迭代协议。`Symbol.asyncIterator` 已支持。

## 生成器

- `function*` / `yield` / `yield*`，以及 `.next()` / `.throw()` / `.return()`，基于同一套栈式协程运行时实现。
- 支持 `for...of`、展开与委托。

## Promise

- 微任务队列（`xt_microtasks`）；`xt_promise_ctor/resolve/reject/static`（含 `withResolvers`）、实例 `then` / `catch` / `finally`。
- `xt_drain_microtasks` 在程序结束时清空队列，事件循环也会在回调之间清空它。
- `xt_await` 对非 Promise / 已 settle 值排入一次微任务，对 pending promise 则挂起当前 async 协程直到 settle。

## 定时器与事件循环

- `setTimeout` / `clearTimeout` / `setInterval` / `clearInterval` / `setImmediate` / `clearImmediate` / `queueMicrotask` 与 `performance.now`（单调递增的小数毫秒）。
- 定时器使用数字 id；未实现 `Timeout` / `Immediate` 对象。
- 定时器 settle 的 promise 可以被 await：async 函数体在 pending promise 上挂起，定时器触发时恢复。套接字与服务端（`net`、`http`、`dgram`）的回调在同一事件循环上投递。
- `fetch` 与 `fs/promises` 由 reactor 驱动而不是阻塞 —— 见下一节。

## 异步 I/O offload

- `runtime/xt_async_io.c` 维护一个小型工作线程池（默认 4 个，惰性创建）。每个 job 分成两半：worker 上的 `run` 只能访问纯 C 内存；主线程上的 `complete` 才构造值并 settle promise。这个拆分让回收器与值模型保持单线程。
- job 完成后通过自管道（POSIX）或回环 UDP 套接字（Windows）唤醒 `select(2)`；reactor 也会轮询线程池，因此唤醒失败只会退化为短暂等待，而不会挂起。
- job 排队或运行期间，每个 job 最多四个值（promise；对 `fetch` 还有 headers 对象）保存在 job 的 root 槽中，由回收器扫描。
- `fetch` 把每一跳请求/重定向作为 job 执行；`fs/promises` 把每个文件与描述符操作作为 job 执行。两者都不会阻塞定时器、套接字或其他 `await`。
- 仍有 job 未完成时事件循环保持存活（`xt_loop_has_work`），线程池空闲后退出。

异步表面的已知限制（`fetch` 无 TLS/流式、`fs.watch` 不触发、线程池固定 4 个 worker）列在[运行时偏差](../unimplemented/deviations.md)与各 Node 页面。
