/**
 * Concrete builtins for Node's `fs` module.
 *
 * Both names map to the same C symbol because the compiler only distinguishes
 * globals, not the namespaces they come from.
 */

import type { BuiltinFunction } from "../../registry.js";

export const readFileBuiltins: Readonly<Record<string, BuiltinFunction>> = {
  readFileSync: { symbol: "xt_node_read_text_file" },
  readTextFile: { symbol: "xt_node_read_text_file" },
};
