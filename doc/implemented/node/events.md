# `events`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/events.md)

Location: `src/extensions/node/events/index.ts`, `runtime/ext_node/events/events.c`

Provides a standalone `EventEmitter`, available both as a **global constructor**
(`new EventEmitter()`) and as a named export
(`import { EventEmitter } from "events"`). Instances share the runtime emitter
used by `stream` / `net` / `http` (listeners live in an internal `__xt_events`
property), with the fuller `events` surface layered on top:

| Method | Notes |
| --- | --- |
| `on(name, fn)` / `addListener(name, fn)` | append a listener |
| `once(name, fn)` | fire at most once, then remove itself |
| `prependListener(name, fn)` / `prependOnceListener(name, fn)` | insert at the front |
| `off(name, fn)` / `removeListener(name, fn)` | remove a listener |
| `removeAllListeners([name])` | clear one event (or all events) |
| `emit(name[, ...args])` | invoke listeners |
| `listeners(name)` / `rawListeners(name)` | listener array |
| `listenerCount(name)` | number of listeners |
| `eventNames()` | names that currently have listeners |
| `setMaxListeners(n)` / `getMaxListeners()` | bookkeeping (default 10) |

Statics reachable through the namespace (`import ee from "events"`):
`listenerCount`, `getEventListeners`, `getMaxListeners`, `setMaxListeners`,
`once`, `addAbortListener`.

```ts
import { EventEmitter } from "events";

const em = new EventEmitter();
em.once("ready", () => console.log("ready"));
em.emit("ready"); // ready
em.emit("ready"); // nothing: the listener already ran
```
