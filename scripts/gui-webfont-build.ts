/**
 * Compile the WOFF/WOFF2 decompression sources into the GUI archive.
 *
 * Kept out of `build-gui.ts` (which has a file-size budget) and next to it so
 * the "what does the `webfonts` feature cost?" question is reviewable in one
 * place. The sources are fetched, not committed (see `fetch-vendored.ts` and
 * the repository's vendor policy): they are compiled once into `gui.a`, and a
 * program that never loads a compressed font never extracts them.
 *
 * Layers, from the outside in:
 *   - `text_webfont.cpp` (the engine, compiled with the other engine objects)
 *     parses the WOFF 1.0 container itself and calls the woff2 reference
 *     decoder for WOFF 2.0;
 *   - `vendor/woff2` reconstructs the transformed `glyf`/`loca`/`hmtx` tables
 *     from the Brotli-compressed stream;
 *   - `vendor/brotli` decodes that stream (decoder only: the encoder, CLI
 *     tools and the shared-dictionary API are not compiled);
 *   - `vendor/miniz`'s `miniz_tinfl.c` inflates each WOFF 1.0 table.
 */

import { join } from "node:path";
import { SECTION_FLAGS } from "./gui-build-options.js";
import { sysrootFlags } from "../src/driver/toolchain.js";

/** Pinned versions; override to test a different decoder release. */
export const BROTLI_VERSION = process.env.BROTLI_VERSION || "1.1.0";
export const WOFF2_VERSION = process.env.WOFF2_VERSION || "1.0.2";
const BROTLI_URL = `https://codeload.github.com/google/brotli/tar.gz/refs/tags/v${BROTLI_VERSION}`;
const WOFF2_URL = `https://codeload.github.com/google/woff2/tar.gz/refs/tags/v${WOFF2_VERSION}`;

/** Brotli files the decoder needs (`common/` holds the static dictionary and
 * the context/transform tables; `dec/` is the decoder itself). */
const BROTLI_COMMON = ["constants", "context", "dictionary", "platform", "shared_dictionary", "transform"];
const BROTLI_DECODER = ["bit_reader", "decode", "huffman", "state"];

/** woff2 files the decoder needs; the encoder/compressor and the CLI are not
 * compiled. */
const WOFF2_SOURCES = ["woff2_dec", "woff2_common", "table_tags", "variable_length", "woff2_out"];

/** Include directories these sources need, for the engine compile as well
 * (`text_webfont.cpp` includes `<woff2/decode.h>` and `miniz_tinfl.h`). */
export function webfontIncludeFlags(vendorDir: string): string[] {
  return [
    "-I", join(vendorDir, "miniz"),
    "-I", join(vendorDir, "brotli", "c", "include"),
    "-I", join(vendorDir, "woff2", "include"),
    "-I", join(vendorDir, "woff2", "src"),
  ];
}

/** Fetch and extract a release tarball into `vendor/<name>`. */
export type FetchSource = (name: string, url: string, extractedName: string) => void;

/**
 * Fetch Brotli and woff2 (an already-extracted copy is reused). This must run
 * before the engine is compiled: `text_webfont.cpp` includes `<woff2/decode.h>`
 * and `miniz_tinfl.h`, so the include directories have to exist by then.
 */
export function fetchWebfontSources(fetchSource: FetchSource): void {
  fetchSource("brotli", BROTLI_URL, `brotli-${BROTLI_VERSION}`);
  fetchSource("woff2", WOFF2_URL, `woff2-${WOFF2_VERSION}`);
}

export interface WebfontBuildOptions {
  readonly vendorDir: string;
  /** Directory the engine objects are written to. */
  readonly objDir: string;
  /** `.o` or `.obj`, matching the archive's ABI. */
  readonly objectSuffix: string;
  /** C and C++ compilers from the same toolchain as the driver. */
  readonly clang: string;
  readonly clangxx: string;
  /** Include directories, including the ones these sources need. */
  readonly includeFlags: readonly string[];
  /** macOS SDK for `-isysroot`; absent elsewhere. */
  readonly sdkRoot?: string;
  /** True for the MSVC ABI (`-fPIC` is rejected there). */
  readonly msvc: boolean;
  /** Run a command, failing the build on a non-zero exit. */
  run(command: string, args: readonly string[]): void;
  /** Fetch and extract a release tarball into `vendor/<name>`. */
  fetchSource: FetchSource;
}

/**
 * Compile Brotli, the woff2 reference decoder and miniz's inflater into
 * `objDir` with the same flags as the engine. Returns the object paths to add
 * to the archive.
 */
export function buildWebfontObjects(options: WebfontBuildOptions): string[] {
  const { vendorDir, objDir, objectSuffix, clang, clangxx, includeFlags, msvc, run } = options;
  fetchWebfontSources(options.fetchSource);

  /* Brotli and miniz are C, so they get the C flags (the C++ standard would be
   * rejected for a `.c` input); woff2 additionally gets the C++ standard.
   * Neither layer is warned about: upstream code is not ours to keep
   * `-Wall`-clean. */
  const common = [
    "-O2",
    ...SECTION_FLAGS,
    ...sysrootFlags(options.sdkRoot),
    ...(msvc ? [] : ["-fPIC"]),
    ...includeFlags,
  ];

  const sources: readonly { readonly source: string; readonly cxx: boolean }[] = [
    ...BROTLI_COMMON.map((name) => ({
      source: join(vendorDir, "brotli", "c", "common", `${name}.c`),
      cxx: false,
    })),
    ...BROTLI_DECODER.map((name) => ({
      source: join(vendorDir, "brotli", "c", "dec", `${name}.c`),
      cxx: false,
    })),
    { source: join(vendorDir, "miniz", "miniz_tinfl.c"), cxx: false },
    ...WOFF2_SOURCES.map((name) => ({
      source: join(vendorDir, "woff2", "src", `${name}.cc`),
      cxx: true,
    })),
  ];

  const objects: string[] = [];
  for (const entry of sources) {
    const name = entry.source.replace(/^.*[\\/]/, "").replace(/\.(?:c|cc)$/, "");
    const object = join(objDir, `webfont_${name}${objectSuffix}`);
    console.log(`xbintsc: compiling ${entry.source.slice(vendorDir.length + 1)}`);
    run(entry.cxx ? clangxx : clang, [
      ...(entry.cxx ? ["-std=c++17"] : []),
      ...common,
      "-c",
      entry.source,
      "-o",
      object,
    ]);
    objects.push(object);
  }
  return objects;
}
