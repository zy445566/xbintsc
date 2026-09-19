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

export const streamModule: NodeModule = {
  name: "stream",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/stream/stream.c")],
  builtins: () => ({}),
};
