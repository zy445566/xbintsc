/**
 * Node's `child_process` module: `spawnSync(command, args, options)`, backed by
 * `runtime/ext_node/child_process`. Captures stdout/stderr as UTF-8 strings,
 * honours `options.cwd`, and supports `options.stdio: "inherit"` (hand the
 * child the parent's stdout/stderr) — which is all the compiler's toolchain
 * wrapper and `xbintsc run` need.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { BuiltinFunction, ModuleExports } from "../../registry.js";

const builtins: Record<string, BuiltinFunction> = {
  spawnSync: { symbol: "xt_child_process_spawn_sync" },
};

const exports: ModuleExports = {
  spawnSync: { symbol: "xt_child_process_spawn_sync" },
};

export const childProcessModule: NodeModule = {
  name: "child_process",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/child_process/child_process.c")],
  builtins: () => builtins,
  exports: () => exports,
};
