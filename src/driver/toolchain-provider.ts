/**
 * Toolchain selection.
 *
 * xbintsc shells out to a clang-compatible driver for three jobs: lowering LLVM
 * IR to an object, compiling the C runtime, and linking. This module decides
 * *which* driver to use, in a fixed priority order, so a bundled toolchain can
 * shadow the system one without touching the driver code:
 *
 *   1. an explicit `xbintsc_CLANG` / `xbintsc_TOOLCHAIN`
 *   2. a toolchain shipped next to the package (`vendor/<os>-<arch>/bin`)
 *   3. the system `PATH` (`findClang`)
 *
 * Extra linker arguments (for example `-fuse-ld=lld`) can be supplied through
 * `xbintsc_LINKER_ARGS`.
 */

import { existsSync } from "node:fs";
import { join } from "node:path";
import { findVendorDir } from "./paths.js";
import { findClang, realRunner, type Runner } from "./toolchain.js";

export type ToolchainSource = "env" | "vendor" | "system";

export interface ResolvedToolchain {
  /** Path or name of the clang-compatible driver to invoke. */
  readonly clang: string;
  /** Where the driver came from. */
  readonly source: ToolchainSource;
  /** Extra arguments to pass to the link step. */
  readonly linkerArgs: readonly string[];
  /**
   * Extra environment for the toolchain subprocesses, merged over the current
   * environment. Used to point `LD_LIBRARY_PATH` at bundled shared libraries
   * (for example the legacy `libtinfo.so.5` the official Linux build needs).
   */
  readonly env: Record<string, string>;
}

function works(runner: Runner, command: string, env?: Record<string, string>): boolean {
  try {
    return runner.run(command, ["--version"], env ? { env } : undefined).status === 0;
  } catch {
    return false;
  }
}

function linkerArgs(fallback: readonly string[]): string[] {
  const raw = process.env.xbintsc_LINKER_ARGS;
  if (raw) return raw.split(" ").filter((part) => part.length > 0);
  return [...fallback];
}

/**
 * Environment overrides for a bundled toolchain. The official Linux build links
 * the removed `libtinfo.so.5` soname, so when we ship it under `vendor/lib/` we
 * prepend that directory to the dynamic linker search path.
 */
function vendorEnv(vendor: string): Record<string, string> {
  const libDir = join(vendor, "lib");
  if (process.platform !== "linux" || !existsSync(libDir)) return {};
  const existing = process.env.LD_LIBRARY_PATH;
  return { LD_LIBRARY_PATH: existing ? libDir + ":" + existing : libDir };
}

/** Resolve the toolchain that `build` should use. See the module docblock. */
export function resolveToolchain(runner: Runner = realRunner): ResolvedToolchain {
  const explicit = process.env.xbintsc_CLANG ?? process.env.xbintsc_TOOLCHAIN;
  if (explicit && works(runner, explicit)) {
    return { clang: explicit, source: "env", linkerArgs: linkerArgs([]), env: {} };
  }

  const vendor = findVendorDir();
  if (vendor) {
    const env = vendorEnv(vendor);
    const names = process.platform === "win32" ? ["clang.exe", "clang"] : ["clang"];
    for (const name of names) {
      const candidate = join(vendor, "bin", name);
      if (existsSync(candidate) && works(runner, candidate, env)) {
        // A bundled toolchain ships `ld.lld`, so prefer lld over any system linker.
        return {
          clang: candidate,
          source: "vendor",
          linkerArgs: linkerArgs(["-fuse-ld=lld"]),
          env,
        };
      }
    }
  }

  return { clang: findClang(runner), source: "system", linkerArgs: linkerArgs([]), env: {} };
}
