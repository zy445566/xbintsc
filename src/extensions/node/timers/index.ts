/**
 * Node's `timers` module: re-exports the timer globals the core runtime already
 * provides (`setTimeout`, `setInterval`, `setImmediate` and their clear forms).
 */

import type { NodeModule } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const symbols: Record<string, string> = {
  setTimeout: "xt_set_timeout",
  clearTimeout: "xt_clear_timeout",
  setInterval: "xt_set_interval",
  clearInterval: "xt_clear_interval",
  setImmediate: "xt_set_immediate",
  clearImmediate: "xt_clear_immediate",
};

const exports: ModuleExports = Object.fromEntries(
  Object.entries(symbols).map(([name, symbol]) => [name, { symbol }]),
);

export const timersModule: NodeModule = {
  name: "timers",
  runtimeSources: () => [],
  builtins: () => ({}),
  exports: () => exports,
};
