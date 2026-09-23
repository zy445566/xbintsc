/**
 * Node's `worker_threads` module.
 *
 * xbintsc emulates a worker by re-running the current executable as a child
 * process; `isMainThread`, `workerData` and `parentPort` become value bindings
 * (nullary runtime getters) and `Worker` is a constructor. See
 * `runtime/ext_node/worker_threads/worker_threads.c`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  Worker: { symbol: "xt_worker_ctor", isConstructor: true },
  isMainThread: { valueSymbol: "xt_worker_is_main_thread" },
  workerData: { valueSymbol: "xt_worker_data" },
  parentPort: { valueSymbol: "xt_worker_parent_port" },
};

export const workerThreadsModule: NodeModule = {
  name: "worker_threads",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/worker_threads/worker_threads.c")],
  builtins: () => ({}),
  exports: () => exports,
};
