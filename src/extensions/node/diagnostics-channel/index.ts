/**
 * Node's `diagnostics_channel` module: `channel` / `tracingChannel`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  channel: { namespace: "diagnostics_channel", method: "channel" },
  tracingChannel: { namespace: "diagnostics_channel", method: "tracingChannel" },
};

export const diagnosticsChannelModule: NodeModule = {
  name: "diagnostics_channel",
  namespace: "diagnostics_channel",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/diagnostics_channel/diagnostics_channel.c")],
  builtins: () => ({}),
  exports: () => exports,
};
