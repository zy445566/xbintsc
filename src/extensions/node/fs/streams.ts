/**
 * Builtins for Node's `fs` streaming factories. xbintsc has no asynchronous
 * streaming, so these return descriptor objects consumed by `stream/promises`.
 */

import type { BuiltinFunction } from "../../registry.js";

export const fsStreamBuiltins: Readonly<Record<string, BuiltinFunction>> = {
  createReadStream: { symbol: "xt_node_create_read_stream" },
  createWriteStream: { symbol: "xt_node_create_write_stream" },
};
