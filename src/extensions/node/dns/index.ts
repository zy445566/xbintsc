/**
 * Node's `dns` module: `lookup`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  lookup: { symbol: "xt_dns_lookup" },
};

export const dnsModule: NodeModule = {
  name: "dns",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/dns/dns.c")],
  builtins: () => ({}),
  exports: () => exports,
};
