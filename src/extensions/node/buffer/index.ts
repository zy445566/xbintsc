/**
 * Node's `Buffer` module.
 *
 * Buffers are ordinary xbintsc objects whose bytes live in an `ArrayBuffer`
 * store behind dynamic-property hooks (plus a `length` property) with methods
 * supplied by the C runtime, so `Buffer.from`, `Buffer.alloc` and the instance
 * methods (`toString`, `slice`, ...) work without a dedicated binary value type
 * in the compiler, and `buf[i]` still reads a byte.
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
  "isUtf8",
  "byteLength",
  "concat",
  "compare",
] as const;

const exports: ModuleExports = {
  Buffer: { symbol: "xt_buffer_ctor", isConstructor: true },
  ...Object.fromEntries(statics.map((method) => [method, { namespace: "Buffer", method }])),
  constants: { valueSymbol: "xt_buffer_constants_value" },
};

export const bufferModule: NodeModule = {
  name: "buffer",
  namespace: "Buffer",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/buffer/buffer.c")],
  builtins: () => ({}),
  exports: () => exports,
};
