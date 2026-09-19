/**
 * Node's `os` module: host information backed by `runtime/ext_node/os`.
 *
 * `os` contributes no global builtins; the compiler lowers `os.<name>(...)` to
 * `xt_os_static(<name>, argc, argv)` (see `NAMESPACE_STATICS` in
 * `src/codegen/generator/tables.ts`). Importing the module aliases that
 * namespace (`import * as os from "os"`) and each named export dispatches to
 * the same runtime entry (`import { platform } from "os"`).
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const methods = [
  "platform",
  "type",
  "arch",
  "endianness",
  "homedir",
  "tmpdir",
  "hostname",
  "release",
  "totalmem",
  "freemem",
  "cpus",
] as const;

const exports: ModuleExports = Object.fromEntries(
  methods.map((method) => [method, { namespace: "os", method }]),
);

export const osModule: NodeModule = {
  name: "os",
  namespace: "os",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/os/os.c")],
  builtins: () => ({}),
  exports: () => exports,
};
