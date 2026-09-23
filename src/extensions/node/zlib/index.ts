/**
 * Node's `zlib` module.
 *
 * Only `createGzip()` is implemented. It returns a descriptor consumed by
 * `stream/promises`' `pipeline`; see `runtime/ext_node/zlib/zlib.c`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";

export const zlibModule: NodeModule = {
  name: "zlib",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/zlib/zlib.c")],
  builtins: () => ({
    createGzip: { symbol: "xt_node_create_gzip" },
  }),
};
