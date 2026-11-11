/**
 * Node's `timers/promises` subpath module: promise-returning timer functions.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  setTimeout: { symbol: "xt_timers_promises_set_timeout" },
  setImmediate: { symbol: "xt_timers_promises_set_immediate" },
};

export const timersPromisesModule: NodeModule = {
  name: "timers/promises",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/timers_promises/timers_promises.c")],
  builtins: () => ({}),
  exports: () => exports,
};
