/**
 * Node's `worker_threads` module.
 *
 * A worker is a second process running the same executable (xbintsc has no
 * threads; see `runtime/worker/`). The worker entry is the current program, so
 * pass `import.meta.filename` and branch on `isMainThread`:
 *
 *   if (isMainThread) {
 *     const worker = new Worker(import.meta.filename, { workerData: "ping" });
 *     worker.on("message", (reply) => console.log(reply));
 *     worker.postMessage("hello");
 *   } else {
 *     parentPort.on("message", (value) => parentPort.postMessage(value));
 *   }
 *
 * `MessageChannel` / `MessagePort` are local to a process; transferring a port
 * to a worker is not implemented.
 */

import type { NodeModule } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  Worker: { symbol: "xt_wt_worker", isConstructor: true },
  isMainThread: { valueSymbol: "xt_wt_is_main_thread" },
  workerData: { valueSymbol: "xt_wt_worker_data" },
  parentPort: { valueSymbol: "xt_wt_parent_port" },
  MessageChannel: { symbol: "xt_wt_message_channel", isConstructor: true },
  MessagePort: { symbol: "xt_wt_message_port", isConstructor: true },
  receiveMessageOnPort: { symbol: "xt_wt_receive_message_on_port" },
  markAsUncloneable: { symbol: "xt_wt_mark_as_uncloneable" },
};

export const workerThreadsModule: NodeModule = {
  name: "worker_threads",
  /** The transport lives in the core runtime; only the bindings are here. */
  runtimeSources: () => [],
  builtins: () => ({}),
  exports: () => exports,
};
