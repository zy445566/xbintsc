/**
 * Node's `http2` module — a stub (HTTP/2 is not implemented).
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  connect: { symbol: "xt_http2_connect" },
  createServer: { symbol: "xt_http2_create_server" },
  createSecureServer: { symbol: "xt_http2_create_secure_server" },
};

export const http2Module: NodeModule = {
  name: "http2",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/http2/http2.c")],
  builtins: () => ({}),
  exports: () => exports,
};
