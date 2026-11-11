/**
 * Node's `zlib` module.
 *
 * Only `createGzip()` is implemented. It returns a descriptor consumed by
 * `stream/promises`' `pipeline`; see `runtime/ext_node/zlib/zlib.c`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  createGzip: { symbol: "xt_node_create_gzip" },
  crc32: { symbol: "xt_node_crc32" },
  constants: { valueSymbol: "xt_zlib_constants_value" },
  Z_DEFAULT_WINDOWBITS: { valueSymbol: "xt_zlib_default_windowbits_value" },
};

export const zlibModule: NodeModule = {
  name: "zlib",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/zlib/zlib.c")],
  builtins: () => ({
    createGzip: { symbol: "xt_node_create_gzip" },
    crc32: { symbol: "xt_node_crc32" },
  }),
  exports: () => exports,
};
