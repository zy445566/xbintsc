#!/usr/bin/env node
/**
 * Verify the release bookkeeping that must agree before a tag is pushed:
 *
 *   1. `COMPILER_VERSION` (reported by `xbintsc version` / `doctor` and embedded
 *      in the incremental cache key) matches the version in `package.json`;
 *   2. every `@xbintsc/<os>-<arch>` optional dependency is either the current
 *      version (a release checkout) or the published `0.0.0` placeholder (the
 *      committed state; release automation rewrites it before `npm publish`).
 *
 * A mismatch means an upgraded compiler can reuse binaries produced by the
 * previous one while presenting the wrong version to bug reports, or that an
 * install would ask npm for a platform package that does not exist.
 */

import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

/** The version of the empty `@xbintsc/*` packages created before a release. */
export const PLATFORM_PLACEHOLDER_VERSION = "0.0.0";

/** Read the version literal from the compiler source. */
export function compilerVersionOf(root: string): string {
  const text = readFileSync(join(root, "src", "driver", "version.ts"), "utf8");
  const match = /export const COMPILER_VERSION = "([^"]+)"/.exec(text);
  if (!match) throw new Error("COMPILER_VERSION is not declared in src/driver/version.ts");
  return match[1]!;
}

/** Read the published version from package.json. */
export function packageVersionOf(root: string): string {
  const parsed = JSON.parse(readFileSync(join(root, "package.json"), "utf8")) as { version?: string };
  if (!parsed.version) throw new Error("package.json has no version field");
  return parsed.version;
}

/** Returns a human-readable mismatch message, or `undefined` when in sync. */
export function checkCompilerVersion(root: string): string | undefined {
  const packageVersion = packageVersionOf(root);
  const compilerVersion = compilerVersionOf(root);
  if (packageVersion === compilerVersion) return undefined;
  return (
    `COMPILER_VERSION is "${compilerVersion}" but package.json is "${packageVersion}".\n` +
    'Update COMPILER_VERSION in src/driver/version.ts (it keys the incremental cache), or fix package.json.'
  );
}

/**
 * Returns a message when a platform optional dependency names a version that is
 * neither the release version nor the placeholder, or `undefined` when valid.
 */
export function checkPlatformVersions(root: string): string | undefined {
  const parsed = JSON.parse(readFileSync(join(root, "package.json"), "utf8")) as {
    version: string;
    optionalDependencies?: Record<string, string>;
  };
  for (const [name, spec] of Object.entries(parsed.optionalDependencies ?? {})) {
    if (!name.startsWith("@xbintsc/")) continue;
    if (spec === parsed.version || spec === PLATFORM_PLACEHOLDER_VERSION) continue;
    return (
      `optional dependency ${name} is "${spec}", but package.json is "${parsed.version}".\n` +
      `Use "${PLATFORM_PLACEHOLDER_VERSION}" in the committed manifest (release automation rewrites it) ` +
      `or "${parsed.version}" in a release checkout.`
    );
  }
  return undefined;
}

const invokedDirectly =
  process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;

if (invokedDirectly) {
  const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
  const problems = [checkCompilerVersion(root), checkPlatformVersions(root)].filter(
    (problem): problem is string => problem !== undefined,
  );
  if (problems.length > 0) {
    process.stderr.write(`xbintsc: version mismatch\n${problems.join("\n")}\n`);
    process.exit(1);
  }
  process.stdout.write(`xbintsc: version ${compilerVersionOf(root)} matches package.json\n`);
}
