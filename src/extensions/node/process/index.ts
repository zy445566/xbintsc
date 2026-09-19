/**
 * Node's `process` object backed by `runtime/ext_node/process`.
 *
 * The compiler lowers `process.<method>(...)` to
 * `xt_process_call(<method>, argc, argv)` and `process.<property>` to
 * `xt_process_get(<property>)` (see `NAMESPACE_STATICS` / `NAMESPACE_PROPERTIES`
 * in `src/codegen/generator/tables.ts`). Importing the module aliases that
 * namespace (`import process from "process"`); named method exports dispatch
 * through `xt_process_call`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const methods = ["cwd", "exit", "uptime", "hrtime", "getuid"] as const;

const exports: ModuleExports = Object.fromEntries(
  methods.map((method) => [method, { namespace: "process", method }]),
);

export const processModule: NodeModule = {
  name: "process",
  namespace: "process",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/process/process.c")],
  builtins: () => ({}),
  exports: () => exports,
};
