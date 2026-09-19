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

export const bufferModule: NodeModule = {
  name: "buffer",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/buffer/buffer.c")],
  builtins: () => ({}),
};
