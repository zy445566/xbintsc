/**
 * Node's `net` module.
 *
 * TCP servers and sockets driven by the core event loop. `net.createServer`,
 * `net.connect`/`net.createConnection` and `net.Socket`/`net.Server` are all
 * backed by `runtime/ext_node/net/net.c`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";

export const netModule: NodeModule = {
  name: "net",
  namespace: "net",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/net/net.c")],
  builtins: () => ({}),
};
