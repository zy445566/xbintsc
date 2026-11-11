/**
 * Node's `tls` module — a stub (TLS is not implemented).
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
