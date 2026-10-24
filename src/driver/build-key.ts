/**
 * The incremental-cache key.
 *
 * Everything that can change the produced binary belongs here: compiler
 * version, source text, emit kind, optimization/LTO/PGO mode, platform,
 * extension and runtime fingerprints, and the embedded application metadata.
 * A feature that adds such an input edits this module (and the call site),
 * not the build orchestration.
 */

import type { ExtensionRegistry } from "../extensions/registry.js";
import { hashParts } from "./cache.js";
import type { AppConfig } from "./config.js";
import { compilerFingerprint, registryFingerprint, runtimeFingerprint } from "./fingerprint.js";
import { COMPILER_VERSION } from "./version.js";
import type { IconInfo } from "./icon.js";
import type { PgoMode } from "./toolchain.js";

export interface CacheKeyInput {
  /** The text the build compiled: the entry source, or the bundled module text. */
  readonly sourceText: string;
  readonly emit: string;
  readonly optimize: string;
  readonly registry: ExtensionRegistry;
  readonly runtimeDir: string;
  readonly icon?: IconInfo;
  readonly app: AppConfig;
  readonly debug: boolean;
  readonly lto: boolean;
  /** Whether the semantic checker ran (default true). */
  readonly check?: boolean;
  readonly pgo?: PgoMode;
  readonly pgoDir: string;
}

/** Hash every input that can change the produced binary. */
export function buildCacheKey(input: CacheKeyInput): string {
  const { app, icon, pgo } = input;
  return hashParts([
    COMPILER_VERSION,
    compilerFingerprint(),
    input.sourceText,
    input.emit,
    input.optimize,
    process.platform,
    registryFingerprint(input.registry),
    runtimeFingerprint(input.runtimeDir),
    icon ? hashParts(["icon", icon.format, Buffer.from(icon.bytes).toString("base64")]) : "",
    app.name ?? "",
    String(app.bundle ?? false),
    app.bundleId ?? "",
    String(input.debug),
    String(input.lto),
    String(input.check !== false),
    pgo ?? "",
    pgo ? input.pgoDir : "",
  ]);
}
