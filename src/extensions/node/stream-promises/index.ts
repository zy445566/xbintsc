/**
 * Node's `stream/promises` module.
 *
 * Provides `pipeline`, implemented synchronously in
 * `runtime/ext_node/stream/pipeline.c` and returned as an already-resolved
 * promise so `await pipeline(...)` works.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";

export const streamPromisesModule: NodeModule = {
  name: "stream/promises",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/stream/pipeline.c")],
  builtins: () => ({
    pipeline: { symbol: "xt_node_stream_pipeline" },
  }),
};
