#!/usr/bin/env node
/**
 * Point `optionalDependencies` at the version being released.
 *
 * The repository keeps every `@xbintsc/<os>-<arch>` entry on the `0.0.0`
 * placeholder: those packages exist on npm (an empty first publish), while a
 * release version does not exist until this workflow publishes it. Committing a
 * not-yet-published version would make `npm ci` resolve a missing tarball.
 *
 * Release automation runs this after the platform tarballs have been validated
 * and published, immediately before `npm publish`, so the published `xbintsc`
 * manifest depends on the platform packages built from the same commit.
 *
 * Usage: `npm run platform-versions -- --version <x.y.z>` (defaults to the
 * version in package.json).
 */

import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

/** Rewrite every platform optional dependency; returns the names changed. */
export function setPlatformVersions(root: string, version: string): string[] {
  const manifestPath = join(root, "package.json");
  const manifest = JSON.parse(readFileSync(manifestPath, "utf8")) as {
    optionalDependencies?: Record<string, string>;
  };
  const dependencies = manifest.optionalDependencies;
  if (!dependencies) throw new Error("package.json has no optionalDependencies");
  const changed: string[] = [];
  for (const name of Object.keys(dependencies)) {
    if (!name.startsWith("@xbintsc/")) continue;
    if (dependencies[name] === version) continue;
    dependencies[name] = version;
    changed.push(name);
  }
  writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
  return changed;
}

const invokedDirectly =
  process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;

if (invokedDirectly) {
  try {
    const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
    const flag = process.argv.indexOf("--version");
    const version =
      flag >= 0 && process.argv[flag + 1] !== undefined
        ? process.argv[flag + 1]!
        : (JSON.parse(readFileSync(join(root, "package.json"), "utf8")) as { version: string }).version;
    const changed = setPlatformVersions(root, version);
    console.log(
      changed.length > 0
        ? `xbintsc: platform optionalDependencies -> ${version} (${changed.length} packages)`
        : `xbintsc: platform optionalDependencies already at ${version}`,
    );
  } catch (error) {
    console.error(`xbintsc: ${error instanceof Error ? error.message : String(error)}`);
    process.exit(1);
  }
}
