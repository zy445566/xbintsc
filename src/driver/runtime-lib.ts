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

import { existsSync, readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, relative, sep } from "node:path";
import { hashParts } from "./cache.js";
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


/** Name of the file recording which sources an archive was built from. */
export const RUNTIME_STAMP_FILE = "runtime-stamp.json";

/** Source files that make up the runtime archives (the archives' own folder is
 *  skipped: it holds the `.a`/`.lib` outputs and the stamp). */
function runtimeSourceFiles(runtimeDir: string): string[] {
  const files: string[] = [];
  const walk = (dir: string): void => {
    let entries: string[];
    try {
      entries = readdirSync(dir);
    } catch {
      return;
    }
    for (const name of entries.sort()) {
      const full = join(dir, name);
      if (statSync(full).isDirectory()) {
        if (name === "lib") continue;
        walk(full);
      } else if (/\.(c|h|inc)$/.test(name)) {
        files.push(full);
      }
    }
  };
  walk(runtimeDir);
  return files;
}

/**
 * Fingerprint of the C runtime sources an archive is compiled from.
 *
 * A prebuilt `core`/`ext_<name>` archive is only usable when it was built from
 * exactly these sources: the driver compares the archive's stamp with this
 * value and compiles from source when they differ. Without the stamp a stale
 * archive links silently whenever every symbol it needs still exists — the
 * program then runs the *old* runtime, which is very hard to diagnose.
 */
export function runtimeSourceFingerprint(runtimeDir: string, compilerVersion: string, flags: readonly string[]): string {
  const parts: string[] = ["runtime-archive", compilerVersion, process.platform, process.arch, [...flags].join(" ")];
  for (const file of runtimeSourceFiles(runtimeDir)) {
    parts.push(relative(runtimeDir, file).split(sep).join("/"), readFileSync(file, "utf8"));
  }
  return hashParts(parts);
}

/** The recorded fingerprint of the archives in `dir`, if any. */
export function readRuntimeStamp(dir: string): string | undefined {
  try {
    const parsed: unknown = JSON.parse(readFileSync(join(dir, RUNTIME_STAMP_FILE), "utf8"));
    if (typeof parsed !== "object" || parsed === null) return undefined;
    const fingerprint = (parsed as { fingerprint?: unknown }).fingerprint;
    return typeof fingerprint === "string" ? fingerprint : undefined;
  } catch {
    return undefined;
  }
}

/** Record the fingerprint next to the archives a build just produced. */
export function writeRuntimeStamp(dir: string, fingerprint: string, compilerVersion: string): void {
  writeFileSync(
    join(dir, RUNTIME_STAMP_FILE),
    `${JSON.stringify({ compilerVersion, fingerprint }, null, "\t")}\n`,
    "utf8",
  );
}

/**
 * A prebuilt archive, but only when it was built from the sources now on disk.
 *
 * Returns `undefined` for a missing archive *and* for a stale one, so the caller
 * falls back to compiling the runtime; `stale` tells the two apart for the
 * message.
 */
export function freshRuntimeLibrary(
  name: string,
  dirs: readonly string[],
  fingerprint: string,
): { path?: string; stale?: string } {
  const path = findRuntimeLibrary(name, dirs);
  if (!path) return {};
  return readRuntimeStamp(dirname(path)) === fingerprint ? { path } : { stale: path };
}
