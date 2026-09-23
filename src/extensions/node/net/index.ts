/**
 * Node's `net` module.
 *
 * TCP servers and sockets driven by the core event loop. `net.createServer`,
 * `net.connect`/`net.createConnection` and `net.Socket`/`net.Server` are all
 * backed by `runtime/ext_node/net/net.c`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const methods = ["createServer", "connect", "createConnection", "isIP", "isIPv4", "isIPv6"] as const;

const exports: ModuleExports = {
  ...Object.fromEntries(methods.map((method) => [method, { namespace: "net", method }])),
  Server: { symbol: "xt_net_server_ctor", isConstructor: true },
  Socket: { symbol: "xt_net_socket_ctor", isConstructor: true },
};

export const netModule: NodeModule = {
  name: "net",
  namespace: "net",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/net/net.c")],
  builtins: () => ({}),
  exports: () => exports,
};
