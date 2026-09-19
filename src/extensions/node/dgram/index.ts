/**
 * Node's `dgram` module.
 *
 * UDP sockets (`dgram.createSocket`) are driven by the core event loop and
 * emit `message` events with an `rinfo` describing the sender.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";

export const dgramModule: NodeModule = {
  name: "dgram",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/dgram/dgram.c")],
  builtins: () => ({}),
};
