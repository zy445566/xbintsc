/**
 * Node's `perf_hooks` module: `performance` (now/timeOrigin) and a no-op
 * `PerformanceObserver`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  performance: { valueSymbol: "xt_perf_hooks_performance_value" },
};

export const perfHooksModule: NodeModule = {
  name: "perf_hooks",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/perf_hooks/perf_hooks.c")],
  builtins: () => ({}),
  exports: () => exports,
};
