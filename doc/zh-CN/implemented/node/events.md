# `events` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/events.md)

位置：`src/extensions/node/events/index.ts`、`runtime/ext_node/events/events.c`

提供独立的 `EventEmitter`，既可以作为**全局构造函数**使用
（`new EventEmitter()`），也可以作为命名导出（`import { EventEmitter } from
"events"`）。实例与 `stream` / `net` / `http` 共用运行时事件发射器（监听器存放
在内部的 `__xt_events` 属性上），并在此基础上提供更完整的 `events` 接口：

| 方法 | 说明 |
| --- | --- |
| `on(name, fn)` / `addListener(name, fn)` | 追加监听器 |
| `once(name, fn)` | 最多触发一次，然后自行移除 |
| `prependListener(name, fn)` / `prependOnceListener(name, fn)` | 插入到最前 |
| `off(name, fn)` / `removeListener(name, fn)` | 移除监听器 |
| `removeAllListeners([name])` | 清空某个（或全部）事件 |
| `emit(name[, ...args])` | 触发监听器 |
| `listeners(name)` / `rawListeners(name)` | 监听器数组 |
| `listenerCount(name)` | 监听器数量 |
| `eventNames()` | 当前有监听器的事件名 |
| `setMaxListeners(n)` / `getMaxListeners()` | 记录（默认 10） |

通过命名空间（`import ee from "events"`）可访问的静态方法：`listenerCount`、
`getEventListeners`、`getMaxListeners`、`setMaxListeners`、`once`、
`addAbortListener`。

```ts
import { EventEmitter } from "events";

const em = new EventEmitter();
em.once("ready", () => console.log("ready"));
em.emit("ready"); // ready
em.emit("ready"); // 无输出：监听器已执行过
```
