/**
 * Node's `Buffer` module.
 *
 * Buffers are represented as plain xbintsc objects (indexed byte properties
 * plus `length`) with methods supplied by the C runtime, so `Buffer.from`,
 * `Buffer.alloc` and the instance methods (`toString`, `slice`, ...) work
 * without a dedicated binary value type in the compiler.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const statics = [
  "from",
  "of",
  "alloc",
  "allocUnsafe",
  "allocUnsafeSlow",
  "isBuffer",
  "byteLength",
  "concat",
  "compare",
] as const;

const exports: ModuleExports = {
  Buffer: { symbol: "xt_buffer_ctor", isConstructor: true },
  ...Object.fromEntries(statics.map((method) => [method, { namespace: "Buffer", method }])),
};

export const bufferModule: NodeModule = {
  name: "buffer",
  namespace: "Buffer",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/buffer/buffer.c")],
  builtins: () => ({}),
  exports: () => exports,
};
