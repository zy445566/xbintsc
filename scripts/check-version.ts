#!/usr/bin/env node
/**
 * Verify that `COMPILER_VERSION` (reported by `xbintsc version` / `doctor` and
 * embedded in the incremental cache key) matches the version in `package.json`
 * (which names the release archives).
 *
 * Release automation runs this before tagging. A mismatch means an upgraded
 * compiler can reuse binaries produced by the previous one while presenting the
 * wrong version to bug reports.
 */

import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

/** Read the version literal from the compiler source. */
export function compilerVersionOf(root: string): string {
  const text = readFileSync(join(root, "src", "driver", "compiler.ts"), "utf8");
  const match = /export const COMPILER_VERSION = "([^"]+)"/.exec(text);
  if (!match) throw new Error("COMPILER_VERSION is not declared in src/driver/compiler.ts");
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
    'Update COMPILER_VERSION in src/driver/compiler.ts (it keys the incremental cache), or fix package.json.'
  );
}

const invokedDirectly =
  process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;

if (invokedDirectly) {
  const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
  const mismatch = checkCompilerVersion(root);
  if (mismatch) {
    process.stderr.write(`xbintsc: version mismatch\n${mismatch}\n`);
    process.exit(1);
  }
  process.stdout.write(`xbintsc: version ${compilerVersionOf(root)} matches package.json\n`);
}
