/**
 * Toolchain selection.
 *
 * xbintsc shells out to a clang-compatible driver for three jobs: lowering LLVM
 * IR to an object, compiling the C runtime, and linking. This module decides
 * *which* driver to use, in a fixed priority order:
 *
 *   1. an explicit `xbintsc_CLANG` / `xbintsc_TOOLCHAIN`
 *   2. the system `PATH` (`findClang`)
 *
 * xbintsc does not ship a compiler. Every host must provide a clang-compatible
 * toolchain (see `doc/requirements.md`): the Xcode Command Line Tools on macOS,
 * the distribution's clang/lld on Linux, and LLVM plus the Visual Studio C++
 * build tools (the MSVC ABI) on Windows.
 *
 * Extra linker arguments (for example `-fuse-ld=lld`) can be supplied through
 * `xbintsc_LINKER_ARGS`.
 */

import { findClang, realRunner, type Runner } from "./toolchain.js";

export type ToolchainSource = "env" | "system";

export interface ResolvedToolchain {
  /** Path or name of the clang-compatible driver to invoke. */
  readonly clang: string;
  /** Where the driver came from. */
  readonly source: ToolchainSource;
  /** Extra arguments to pass to the link step. */
  readonly linkerArgs: readonly string[];
  /** Extra environment for the toolchain subprocesses, merged over the current
   * environment. */
  readonly env: Record<string, string>;
}

function works(runner: Runner, command: string): boolean {
  try {
    return runner.run(command, ["--version"]).status === 0;
  } catch {
    return false;
  }
}

function linkerArgs(fallback: readonly string[]): string[] {
  const raw = process.env.xbintsc_LINKER_ARGS;
  if (raw) return raw.split(" ").filter((part) => part.length > 0);
  return [...fallback];
}

/** Resolve the toolchain that `build` should use. See the module docblock. */
export function resolveToolchain(runner: Runner = realRunner): ResolvedToolchain {
  const explicit = process.env.xbintsc_CLANG ?? process.env.xbintsc_TOOLCHAIN;
  if (explicit && works(runner, explicit)) {
    return { clang: explicit, source: "env", linkerArgs: linkerArgs([]), env: {} };
  }

  return { clang: findClang(runner), source: "system", linkerArgs: linkerArgs([]), env: {} };
}
