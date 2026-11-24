/**
 * Compile the GUI engine translation units (`runtime/ext_gui/*.cpp`) into the
 * objects that are merged into `gui.a`/`gui.lib`.
 *
 * Split out of `build-gui.ts` (which has a file-size budget) so the engine
 * compile flags live next to the "one function/data item per section, one
 * runtime ABI" policy they implement. The vendored libraries are built by CMake
 * in `build-gui.ts`; this module only covers the engine itself.
 */

import { readdirSync } from "node:fs";
import { join } from "node:path";
import { cxxCompileFlags } from "./gui-build-options.js";

export interface EngineBuildOptions {
  /** `runtime/ext_gui`. */
  readonly engineDir: string;
  /** Directory the objects are written to (`build/gui-obj`). */
  readonly objDir: string;
  /** `.o` or `.obj`, matching the archive's ABI. */
  readonly objectSuffix: string;
  /** `clang++` from the driver's toolchain (never a bare `clang++`). */
  readonly clangxx: string;
  /** macOS SDK for `-isysroot`; absent elsewhere. */
  readonly sdkRoot?: string;
  /** Include directories, including the vendored headers the engine uses. */
  readonly includeFlags: readonly string[];
  /** True for the MSVC ABI: record the static C++ runtime and skip `-fPIC`. */
  readonly msvc: boolean;
  /** Run a compiler command, failing the build on a non-zero exit. */
  run(command: string, args: readonly string[]): void;
}

/** Compile every engine translation unit and return the object paths. */
export function buildEngineObjects(options: EngineBuildOptions): string[] {
  const { engineDir, objDir, objectSuffix, clangxx, includeFlags, msvc, run } = options;
  const flags = cxxCompileFlags(options.sdkRoot, includeFlags);
  /* The engine is C++ and the MSVC C++ runtime must match the static C CRT that
   * clang links by default, so let every engine object record `libcpmt` as a
   * default library. The extension itself only contributes the DLL import
   * library (`-lmsvcprt`, `src/extensions/gui/index.ts`), which is inconsistent
   * with that CRT and makes the final link fail with `LNK2038`/unresolved
   * `__imp_*`. */
  if (msvc) flags.push("-Xclang", "--dependent-lib=libcpmt");
  /* The MSVC ABI rejects `-fPIC`; position independence is the default
   * elsewhere. */
  if (!msvc) flags.splice(3, 0, "-fPIC");

  const sources = readdirSync(engineDir)
    .filter((entry) => entry.endsWith(".cpp"))
    .sort();
  return sources.map((source) => {
    const object = join(objDir, source.replace(/\.cpp$/, objectSuffix));
    console.log(`xbintsc: compiling ${source}`);
    run(clangxx, [...flags, "-c", join(engineDir, source), "-o", object]);
    return object;
  });
}
