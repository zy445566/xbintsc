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
}

function works(runner: Runner, command: string): boolean {
  try {
    return runner.run(command, ["--version"]).status === 0;
  } catch {
    return false;
  }
}

function linkerArgsFromEnv(): string[] {
  const raw = process.env.xbintsc_LINKER_ARGS;
  return raw ? raw.split(" ").filter((part) => part.length > 0) : [];
}

/** Resolve the toolchain that `build` should use. See the module docblock. */
export function resolveToolchain(runner: Runner = realRunner): ResolvedToolchain {
  const linkerArgs = linkerArgsFromEnv();

  const explicit = process.env.xbintsc_CLANG ?? process.env.xbintsc_TOOLCHAIN;
  if (explicit && works(runner, explicit)) {
    return { clang: explicit, source: "env", linkerArgs };
  }

  const vendor = findVendorDir();
  if (vendor) {
    const names = process.platform === "win32" ? ["clang.exe", "clang"] : ["clang"];
    for (const name of names) {
      const candidate = join(vendor, "bin", name);
      if (existsSync(candidate) && works(runner, candidate)) {
        return { clang: candidate, source: "vendor", linkerArgs };
      }
    }
  }

  return { clang: findClang(runner), source: "system", linkerArgs };
}
