/**
 * Node's `readline/promises` subpath module: promise-based `createInterface`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  createInterface: { symbol: "xt_readline_promises_create_interface" },
};

export const readlinePromisesModule: NodeModule = {
  name: "readline/promises",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/readline_promises/readline_promises.c")],
  builtins: () => ({}),
  exports: () => exports,
};
