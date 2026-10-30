/**
 * Public option and result types for the build driver.
 *
 * They live apart from the orchestration in `compiler.ts` so a feature that
 * adds one option touches this file (and the phase that consumes it) without
 * colliding with unrelated driver work.
 */

import type { Diagnostic } from "../diagnostics/diagnostic.js";
import type { ExtensionRegistry } from "../extensions/registry.js";
import type { AppConfig } from "./config.js";
import type { PgoMode, Runner } from "./toolchain.js";

/** What `build` produces: an executable, an object file or LLVM IR only. */
export type EmitKind = "exe" | "obj" | "ir";

export interface BuildOptions {
  /** Explicit output path (defaults to the source base name in `outDir`). */
  readonly output?: string;
  readonly outDir?: string;
  readonly optimize?: "0" | "1" | "2" | "3";
  readonly emit?: EmitKind;
  readonly force?: boolean;
  readonly verbose?: boolean;
  readonly cacheDir?: string;
  readonly extensions?: ExtensionRegistry;
  readonly runner?: Runner;
  readonly clang?: string;
  /** Prefer a prebuilt `runtime/lib/<os>-<arch>/*.a` archive when present (default true). */
  readonly preferPrebuilt?: boolean;
  readonly write?: boolean;
  /** Application icon embedded into the binary (shorthand for `app.icon`). */
  readonly icon?: string;
  /** Application metadata: name, icon and macOS bundle settings. */
  readonly app?: AppConfig;
  /** Emit LLVM debug metadata so `-g` produces DWARF/CodeView (default false). */
  readonly debug?: boolean;
  /**
   * Run the semantic checker before lowering (default true). `--no-check`
   * disables it; the incremental cache keys on this flag, so a checked and an
   * unchecked build of the same source cannot reuse each other's artifacts.
   */
  readonly check?: boolean;
  /** Compile the program and runtime to LTO bitcode and link with lld. */
  readonly lto?: boolean;
  /**
   * Profile-guided optimization. `generate` builds an instrumented binary that
   * writes `.profraw` files into `pgoDir`; `use` consumes them.
   */
  readonly pgo?: PgoMode;
  /** Profile directory (default: `<outDir>/pgo`). */
  readonly pgoDir?: string;
  /**
   * Optional link features to force on (`media`, `gpu`, `images`, `shaping`, `webgl`, or `all`). Features are
   * otherwise selected from a static scan of the bundled program; forcing one
   * is the escape hatch for HTML/CSS built from runtime data.
   */
  readonly features?: readonly string[];
  /** Optional link features to keep out even when the scan detects a use. */
  readonly noFeatures?: readonly string[];
}

/** One phase of a build, in milliseconds (reported by `--time`). */
export interface PhaseTiming {
  readonly phase: "frontend" | "codegen" | "runtime" | "clang" | "link";
  readonly ms: number;
}

export interface BuildResult {
  readonly outputPath: string;
  readonly irPath?: string;
  readonly cached: boolean;
  readonly diagnostics: readonly Diagnostic[];
  readonly ir?: string;
  /** macOS `.app` bundle produced by `app.bundle` (darwin only). */
  readonly bundlePath?: string;
  /** Per-phase timings, collected for `xbintsc build --time`. */
  readonly timings?: readonly PhaseTiming[];
  /** Active optional link features (only for a linked `exe` build). */
  readonly features?: readonly string[];
}

export interface CompileStringResult {
  readonly ir: string;
  readonly diagnostics: readonly Diagnostic[];
}
