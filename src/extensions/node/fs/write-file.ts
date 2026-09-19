/**
 * Builtins for Node's `fs` write/append APIs.
 */

import type { BuiltinFunction } from "../../registry.js";

export const writeFileBuiltins: Readonly<Record<string, BuiltinFunction>> = {
  writeFileSync: { symbol: "xt_node_write_file" },
  appendFileSync: { symbol: "xt_node_append_file" },
};
