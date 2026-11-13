# Web Worker

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/web-worker.md)

Location: `src/extensions/webworker/index.ts`, `runtime/worker/`

Enable the extension with `--ext webworker` (or `"extensions": ["webworker"]`
in `xbintsc.config.json`). It adds the browser globals `Worker`,
`MessageChannel`, `MessagePort`, `self`, `isMainThread` and the bare
`postMessage` function used inside a worker script.

## How a worker runs

xbintsc has no threads: the value model, the collector and the event loop are
single-threaded. A worker is a **second process** running the same executable.
`new Worker(...)` starts it, the two processes talk over a loopback control
socket, and every message is structured-cloned in the sender and rebuilt in the
receiver (arrays, objects, `Map`, `Set`, `Date`, `RegExp`, `ArrayBuffer`,
typed arrays, `BigInt`, `undefined`, shared references and cycles).

Because the binary is compiled ahead of time, the only worker script it can run
is itself. Pass `import.meta.filename` and branch on the `isMainThread` global:

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

A different script path throws an actionable error instead of silently running
the wrong code.

## Surface

| API | Notes |
| --- | --- |
| `new Worker(url, options?)` | `url` must name this executable (`import.meta.filename`); `options.name` becomes `self.name`; `options.workerData` is not a Web Worker option (use the Node API) |
| `worker.postMessage(value, transfer?)` | The value is structured-cloned; `transfer` is accepted but copies |
| `worker.terminate()` | Kills the worker process and emits `exit` |
| `worker.onmessage` / `onmessageerror` / `onerror` | Receive `MessageEvent`-shaped objects (`type`, `data`, `target`) |
| `worker.addEventListener(type, fn)` / `removeEventListener` | `message`, `error`, `messageerror` |
| `self.postMessage(value)` | Worker → parent |
| `self.onmessage` / `addEventListener` | Parent → worker |
| `self.close()` | Stops the worker; the process exits once its loop drains |
| `self.name` | The `name` the parent passed |
| `postMessage(value)` | The bare global form of `self.postMessage` inside a worker |
| `new MessageChannel()` | `port1`/`port2`, with `postMessage`, `onmessage`, `addEventListener`, `close` |

Both conventions share one event list, so `worker.on("message", fn)` (Node) and
`worker.onmessage = fn` can be mixed: the Node listener receives the raw value,
the browser listener receives the `MessageEvent`. The same names are also
importable from the `webworker` module
(`import { Worker, self, isMainThread } from "webworker"`).

## Lifetime

The parent process stays alive while a worker runs. A worker stays alive while
it has a message listener (or other event-loop work such as a timer); a worker
that only posts and returns exits on its own. `self.close()` or
`worker.terminate()` ends it early, and the parent then emits `exit`.

## Combining with the GUI extension

`gui` also defines a `self` global (the window). `webworker` is registered after
`gui`, so when both extensions are enabled `self` is the worker scope and a page
uses `window` for the window object. Do not rely on `self === window` in a page
built with both.

## Limitations

- A worker cannot run a separate source file: everything is one binary, so the
  script is the current program and the program branches on `isMainThread`.
- A `MessagePort` cannot be transferred to another process yet; sending one
  throws `MessagePort could not be cloned.`
- `importScripts`, `SharedArrayBuffer` and real parallelism are not available
  (workers are processes; values are copied, never shared).
- An uncaught exception in a worker prints on stderr and ends that process with
  a non-zero code; the parent observes the `exit` event, not `error`.
