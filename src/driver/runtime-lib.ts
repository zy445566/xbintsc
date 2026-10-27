/**
 * Prebuilt runtime archives.
 *
 * The C runtime is normally compiled once into a static archive by
 * `npm run runtime`:
 *
 *   runtime/lib/<os>-<arch>/core.a        (the core `xt_*.c` runtime)
 *   runtime/lib/<os>-<arch>/ext_<name>.a  (per-extension sources, e.g. ext_node.a)
 *
 * A source checkout or a release archive carries them in the package itself.
 * An npm install instead receives them from the optional
 * `@xbintsc/<os>-<arch>` package (installed only for the host platform), which
 * uses the same `runtime/lib/<os>-<arch>/` layout. The driver reads that folder
 * in place, so no install script has to copy anything into `node_modules`.
 *
 * Shipping these lets `build` link without compiling C, and shortens the first
 * build. When an archive is missing the driver transparently falls back to
 * compiling the `.c` sources, so the archives are an optimization, not a hard
 * dependency.
 */

import { existsSync, readdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { findRuntimeDir, platformSlug } from "./paths.js";

/** Archive suffixes, in the order a name is probed. */
const ARCHIVE_SUFFIXES = [".a", ".lib"] as const;

/**
 * Folder holding the archives for the current host inside the xbintsc package
 * itself (a source checkout or an unpacked release archive).
 */
function packagedRuntimeLibDir(): string {
  return join(findRuntimeDir(), "lib", platformSlug());
}

/**
 * Archive folders of an installed `@xbintsc/<os>-<arch>` package, given the
 * root of the installed xbintsc package.
 *
 * Package managers place the optional dependency either inside xbintsc's own
 * `node_modules` or in the nearest hoisted `node_modules`, so every ancestor is
 * probed. Yarn PnP has no `node_modules`; there the driver simply falls back to
 * compiling the runtime sources.
 */
export function platformRuntimeLibDirs(packageRoot: string, slug: string = platformSlug()): string[] {
  const dirs = new Set<string>();
  let dir = packageRoot;
  for (;;) {
    dirs.add(join(dir, "node_modules", "@xbintsc", slug, "runtime", "lib", slug));
    const parent = dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return [...dirs];
}

/** Every folder searched for a prebuilt archive, most specific first. */
export function runtimeLibDirs(): readonly string[] {
  return [packagedRuntimeLibDir(), ...platformRuntimeLibDirs(dirname(findRuntimeDir()))];
}

/** True when a folder holds at least one runtime archive. */
function containsArchive(dir: string): boolean {
  try {
    return readdirSync(dir).some((entry) => ARCHIVE_SUFFIXES.some((suffix) => entry.endsWith(suffix)));
  } catch {
    return false;
  }
}

/**
 * Directory holding the prebuilt archives for the current host: the package's
 * own `runtime/lib/<slug>`, or the optional platform package when the former
 * has no archives (a normal npm install).
 */
export function runtimeLibDir(dirs: readonly string[] = runtimeLibDirs()): string {
  return dirs.find((dir) => containsArchive(dir)) ?? dirs[0]!;
}

/**
 * Absolute path to a prebuilt archive (`core`, `ext_node`, `gui`, `media`, …),
 * if present. `dirs` is injectable for tests.
 */
export function findRuntimeLibrary(
  name: string,
  dirs: readonly string[] = runtimeLibDirs(),
): string | undefined {
  for (const dir of dirs) {
    for (const suffix of ARCHIVE_SUFFIXES) {
      const candidate = join(dir, `${name}${suffix}`);
      if (existsSync(candidate)) return candidate;
    }
  }
  return undefined;
}
