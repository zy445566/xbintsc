/**
 * The `compression` link feature: miniz DEFLATE for `node:zlib`.
 *
 * The always-linked `zlib` module exposes `deflateRawSync` etc. with a
 * stored-block (uncompressed) fallback. When a program actually reaches for
 * compression, this feature compiles and links the miniz single-file library
 * and installs its raw-DEFLATE backend into the `zlib` module.
 */

import type { LinkFeature, ProgramUsage } from "../../link-feature.js";
import { resolveFrom } from "../module.js";

const COMPRESSION_IDENTIFIERS: ReadonlySet<string> = new Set([
  "deflateRawSync",
  "deflateSync",
  "inflateSync",
  "gzipSync",
  "gunzipSync",
  "unzipSync",
  "createDeflate",
  "createInflate",
  "createGzip",
  "createGunzip",
  "createUnzip",
  "createInflateRaw",
  "createDeflateRaw",
  "brotliCompressSync",
  "brotliDecompressSync",
  "createBrotliCompress",
  "createBrotliDecompress",
  "zstdCompressSync",
  "zstdDecompressSync",
  "createZstdCompress",
  "createZstdDecompress",
]);

export const compressionFeature: LinkFeature = {
  name: "compression",
  description: "miniz DEFLATE compression/decompression for node:zlib",
  runtimeSources: () => [
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/zlib/miniz/miniz.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/zlib/miniz/miniz_tdef.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/zlib/miniz/miniz_tinfl.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/zlib/miniz/compression_bridge.c"),
  ],
  used: (usage: ProgramUsage): boolean =>
    [...usage.identifiers].some((name) => COMPRESSION_IDENTIFIERS.has(name)),
};
