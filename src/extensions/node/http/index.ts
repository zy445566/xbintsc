/**
 * Node's `http` module.
 *
 * `http.createServer` wraps a `net` server and parses requests into a
 * `req`/`res` pair; `http.get`/`http.request` wrap a `net` socket and surface
 * the response through an EventEmitter. Both are implemented in
 * `runtime/ext_node/http/http.c`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";

export const httpModule: NodeModule = {
  name: "http",
  runtimeSources: () => [
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/net/net.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/http/http.c"),
  ],
  builtins: () => ({}),
};
