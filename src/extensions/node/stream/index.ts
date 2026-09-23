/**
 * Node's `stream` module.
 *
 * `Readable`, `Writable`, `Duplex`, `Transform` and `PassThrough` are provided
 * as global constructors (`new Transform(...)`); `stream.Readable.from(...)`
 * resolves through the `stream` namespace. Streams are EventEmitters backed by
 * the C runtime's synchronous stream implementation.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  Readable: { symbol: "xt_readable_ctor", isConstructor: true },
  Writable: { symbol: "xt_writable_ctor", isConstructor: true },
  Duplex: { symbol: "xt_duplex_ctor", isConstructor: true },
  Transform: { symbol: "xt_transform_ctor", isConstructor: true },
  PassThrough: { symbol: "xt_pass_through_ctor", isConstructor: true },
};

export const streamModule: NodeModule = {
  name: "stream",
  namespace: "stream",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/stream/stream.c")],
  builtins: () => ({}),
  exports: () => exports,
};
