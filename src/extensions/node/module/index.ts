/**
 * Node's `module` module: `createRequire` / `isBuiltin`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  createRequire: { symbol: "xt_module_create_require" },
  isBuiltin: { symbol: "xt_module_is_builtin" },
};

export const moduleModule: NodeModule = {
  name: "module",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/module/module.c")],
  builtins: () => ({}),
  exports: () => exports,
};
