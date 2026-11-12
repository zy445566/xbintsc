/**
 * Node's `tls` module — importable bindings.
 *
 * The implementation is the optional `node-tls` link feature (mbedTLS-backed
 * `TLSSocket`, `connect` and `createServer`); this module keeps the import
 * surface stable and forwards to it. See `feature.ts` for feature selection.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  connect: { symbol: "xt_tls_connect" },
  createServer: { symbol: "xt_tls_create_server" },
  TLSSocket: { symbol: "xt_tls_socket_ctor", isConstructor: true },
};

export const tlsModule: NodeModule = {
  name: "tls",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/tls/tls.c")],
  builtins: () => ({}),
  exports: () => exports,
};
