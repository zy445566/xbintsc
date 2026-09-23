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
import type { ModuleExports } from "../../registry.js";

const methods = ["createServer", "request", "get"] as const;

const exports: ModuleExports = Object.fromEntries(
  methods.map((method) => [method, { namespace: "http", method }]),
);

export const httpModule: NodeModule = {
  name: "http",
  namespace: "http",
  runtimeSources: () => [
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/net/net.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/http/http.c"),
  ],
  builtins: () => ({}),
  exports: () => exports,
};
