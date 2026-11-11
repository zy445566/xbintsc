/**
 * Node's `zlib` module.
 *
 * `createGzip()` returns a descriptor consumed by `stream/promises` `pipeline`;
 * `crc32` and `deflateRawSync` are implemented (the latter via the optional
 * `compression` link feature, miniz). The remaining `create*` decompressors
 * return descriptors and the `zstd*` sync helpers are stubs. See
 * `runtime/ext_node/zlib/zlib.c`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  createGzip: { symbol: "xt_node_create_gzip" },
  crc32: { symbol: "xt_node_crc32" },
  constants: { valueSymbol: "xt_zlib_constants_value" },
  Z_DEFAULT_WINDOWBITS: { valueSymbol: "xt_zlib_default_windowbits_value" },
  deflateRawSync: { symbol: "xt_node_deflate_raw_sync" },
  createInflate: { symbol: "xt_node_create_inflate" },
  createGunzip: { symbol: "xt_node_create_gunzip" },
  createInflateRaw: { symbol: "xt_node_create_inflate_raw" },
  createBrotliDecompress: { symbol: "xt_node_create_brotli_decompress" },
  createZstdDecompress: { symbol: "xt_node_create_zstd_decompress" },
  zstdCompressSync: { symbol: "xt_node_zstd_compress_sync" },
  zstdDecompressSync: { symbol: "xt_node_zstd_decompress_sync" },
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
