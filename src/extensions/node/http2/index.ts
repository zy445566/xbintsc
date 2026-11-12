/**
 * Node's `http2` module — importable bindings.
 *
 * The implementation is the optional `http2` link feature (nghttp2 over
 * cleartext h2c or TLS/ALPN `h2`); this module keeps the import surface stable
 * and forwards to it. See `feature.ts` for the feature selection.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  connect: { symbol: "xt_http2_connect" },
  createServer: { symbol: "xt_http2_create_server" },
  createSecureServer: { symbol: "xt_http2_create_secure_server" },
  constants: { valueSymbol: "xt_http2_constants" },
};

export const http2Module: NodeModule = {
  name: "http2",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/http2/http2.c")],
  builtins: () => ({}),
  exports: () => exports,
};
