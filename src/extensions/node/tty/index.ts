/**
 * Node's `tty` module: `isatty(fd)`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  isatty: { symbol: "xt_tty_isatty" },
};

export const ttyModule: NodeModule = {
  name: "tty",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/tty/tty.c")],
  builtins: () => ({}),
  exports: () => exports,
};
