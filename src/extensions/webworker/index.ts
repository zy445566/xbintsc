/**
 * Web Worker API.
 *
 * xbintsc has no threads: the value model, the collector and the event loop are
 * single-threaded. A worker is therefore a **second process** running the same
 * executable (see `runtime/worker/`). The public surface follows the Web
 * Worker API:
 *
 *   const worker = new Worker(import.meta.filename);
 *   worker.onmessage = (event) => console.log(event.data);
 *   worker.postMessage("ping");
 *
 *   // inside the worker process
 *   self.onmessage = (event) => self.postMessage(event.data.toUpperCase());
 *
 * Because the binary is compiled ahead of time, the only worker script it can
 * run is itself: pass `import.meta.filename` and branch on the `isMainThread`
 * global (the same flag Node's `worker_threads` exports).
 *
 * `MessageChannel` / `MessagePort` work within one process (a worker cannot
 * transfer a port to its parent yet).
 */

import type { Extension, ExtensionModule, GlobalValue } from "../registry.js";

const globals: Record<string, GlobalValue> = {
  Worker: { symbol: "xt_webworker_worker" },
  MessageChannel: { symbol: "xt_webworker_message_channel" },
  MessagePort: { symbol: "xt_webworker_message_port" },
  /** The worker global scope. In the main thread it exists but `postMessage`
   *  throws: messages go through a `Worker` object there. */
  self: { symbol: "xt_webworker_self" },
  /** True in the main program, false inside a worker. */
  isMainThread: { symbol: "xt_webworker_is_main_thread" },
};

const webworkerModule: ExtensionModule = {
  namespace: "webworker",
  exports: {
    Worker: { symbol: "xt_webworker_worker_new", isConstructor: true },
    MessageChannel: { symbol: "xt_webworker_message_channel_new", isConstructor: true },
    MessagePort: { symbol: "xt_webworker_message_port_new", isConstructor: true },
    self: { valueSymbol: "xt_webworker_self" },
    isMainThread: { valueSymbol: "xt_webworker_is_main_thread" },
    postMessage: { symbol: "xt_webworker_post_message" },
  },
};

export const webworkerExtension: Extension = {
  name: "webworker",
  description: "Web Worker API (Worker, self, MessageChannel) backed by process-isolated workers",
  globalValues: () => globals,
  builtins: () => ({
    /** Bare `postMessage(data)` inside a worker, as browser scripts use it. */
    postMessage: { symbol: "xt_webworker_post_message" },
  }),
  modules: () => ({ webworker: webworkerModule }),
};
