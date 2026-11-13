/**
 * WebAssembly execution, contributed as the optional `wasm` link feature.
 *
 * `WebAssembly` is a browser namespace recognised by the core runtime, but the
 * interpreter behind it (wasm3) is not something every program should link.
 * The driver scans the bundled program for the identifier and compiles the
 * bridge plus the pinned wasm3 sources only when it is reached. A program that
 * never mentions `WebAssembly` links neither.
 *
 * wasm3 is vendored by commit in `vendor/wasm3` (see
 * `scripts/fetch-vendored.ts`); the bridge installs its dispatch table from a
 * constructor, which is what the core `xt_webassembly.c` calls into.
 */

import { existsSync } from "node:fs";
import { join } from "node:path";
import type { LinkFeature, ProgramUsage } from "./link-feature.js";
import { findRuntimeDir, findVendorDir } from "../driver/paths.js";

/**
 * The wasm3 translation units the interpreter needs. The API modules
 * (`m3_api_*`) are deliberately absent: xbintsc supplies its own JS import
 * bridge instead of wasi/libc, and the remaining `m3_*` files are the parser,
 * validator, compiler and interpreter proper.
 */
const WASM3_SOURCES = [
  "m3_bind.c",
  "m3_code.c",
  "m3_compile.c",
  "m3_core.c",
  "m3_deterministic.c",
  "m3_env.c",
  "m3_exec.c",
  "m3_function.c",
  "m3_info.c",
  "m3_module.c",
  "m3_parse.c",
  "m3_snapshot.c",
  "m3_validate.c",
  "m3_xxh64.c",
] as const;

/** True when the bundled program can reach `WebAssembly`. */
export function hasWasmUsage(usage: ProgramUsage): boolean {
  return usage.identifiers.has("WebAssembly");
}

export const wasmFeature: LinkFeature = {
  name: "wasm",
  description: "WebAssembly execution backed by the vendored wasm3 interpreter",
  /* A program that uses WebAssembly cannot degrade to the core stub: the
   * missing-vendor failure has to be a build error with the `npm run vendor`
   * instruction, not a warning followed by an unresolved symbol at link time. */
  required: true,
  runtimeSources: () => {
    const runtime = findRuntimeDir();
    const vendor = join(findVendorDir(), "wasm3");
    if (!existsSync(join(vendor, "wasm3.h"))) {
      throw new Error(
        "wasm3 sources are missing (vendor/wasm3/wasm3.h); run `npm run vendor` in a source " +
          "checkout, or reinstall xbintsc so the package's vendored sources are present.",
      );
    }
    return [
      ...WASM3_SOURCES.map((source) => join(vendor, source)),
      join(runtime, "xt_wasm", "wasm_bridge.c"),
      join(runtime, "xt_wasm", "wasm_api.c"),
      join(runtime, "xt_wasm", "wasm_values.c"),
      join(runtime, "xt_wasm", "wasm_module.c"),
      join(runtime, "xt_wasm", "wasm_instance.c"),
    ];
  },
  used: hasWasmUsage,
};
