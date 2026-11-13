/**
 * Node's `process` object backed by `runtime/ext_node/process`.
 *
 * The compiler lowers `process.<method>(...)` to
 * `xt_process_call(<method>, argc, argv)`, `process.<property>` to
 * `xt_process_get(<property>)` and `process.<property> = value` to
 * `xt_process_set(<property>, value)` (see `NAMESPACE_STATICS` /
 * `NAMESPACE_PROPERTIES` / `NAMESPACE_PROPERTY_SETTERS` in
 * `src/codegen/generator/tables.ts`). Importing the module aliases that
 * namespace (`import process from "process"`); named method exports dispatch
 * through `xt_process_call`.
 *
 * The object is also an EventEmitter (`on`/`once`/`off`/`emit`, with "exit"
 * fired before the process ends) and owns the three standard streams, so
 * `process.stdin.setRawMode(true)` and `process.stdout.columns` reach the same
 * singletons.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const methods = [
  "cwd",
  "exit",
  "uptime",
  "hrtime",
  "getuid",
  "on",
  "once",
  "addListener",
  "prependListener",
  "off",
  "removeListener",
  "removeAllListeners",
  "emit",
  "nextTick",
] as const;

const exports: ModuleExports = Object.fromEntries(
  methods.map((method) => [method, { namespace: "process", method }]),
);

export const processModule: NodeModule = {
  name: "process",
  namespace: "process",
  runtimeSources: () => [
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/process/process.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/process/streams.c"),
  ],
  builtins: () => ({}),
  exports: () => exports,
};
