/**
 * Node's `child_process` module: `spawnSync(command, args, options)`, backed by
 * `runtime/ext_node/child_process`. Captures stdout/stderr as UTF-8 strings,
 * honours `options.cwd` and `options.env` (replace semantics, like Node), and
 * supports `options.stdio: "inherit"` (hand the child the parent's
 * stdout/stderr) — which is all the compiler's toolchain wrapper and
 * `xbintsc run` need.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { BuiltinFunction, ModuleExports } from "../../registry.js";

const builtins: Record<string, BuiltinFunction> = {
  spawnSync: { symbol: "xt_child_process_spawn_sync" },
};

const exports: ModuleExports = {
  spawnSync: { symbol: "xt_child_process_spawn_sync" },
  spawn: { symbol: "xt_child_process_spawn" },
  execFile: { symbol: "xt_child_process_exec_file" },
  execSync: { symbol: "xt_child_process_exec_sync" },
};

export const childProcessModule: NodeModule = {
  name: "child_process",
  runtimeSources: () => [
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/child_process/child_process.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/child_process/spawn.c"),
    resolveFrom(
      import.meta.url,
      process.platform === "win32"
        ? "../../../../runtime/ext_node/child_process/spawn_win.c"
        : "../../../../runtime/ext_node/child_process/spawn_posix.c",
    ),
  ],
  builtins: () => builtins,
  exports: () => exports,
};
