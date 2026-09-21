/** Filesystem locations owned by the compiler package. */

import { existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));

/** Directory containing `rt.h` and the runtime/ext C sources. */
export function findRuntimeDir(): string {
  const candidates = [
    resolve(here, "../../runtime"),
    resolve(here, "../../../runtime"),
    // Packaged release layout: `<root>/bin/xbintsc` next to `<root>/runtime`.
    resolve(here, "../runtime"),
    resolve(here, "runtime"),
    resolve(process.cwd(), "runtime"),
  ];
  for (const candidate of candidates) {
    if (existsSync(join(candidate, "rt.h"))) return candidate;
  }
  throw new Error("Unable to locate the xbintsc runtime directory (missing runtime/rt.h)");
}

export function findPackageRoot(): string {
  return resolve(findRuntimeDir(), "..");
}

/** `<package>/vendor` — where a bundled toolchain is assembled (may not exist). */
export function vendorRootDir(): string {
  return resolve(findPackageRoot(), "vendor");
}

/** Platform-architecture slug, e.g. `darwin-arm64`, `linux-x64`, `win32-x64`. */
export function platformSlug(): string {
  return `${process.platform}-${process.arch}`;
}

/**
 * Base name (no extension) of a packaged release archive, e.g.
 * `xbintsc-linux-x64`. Matches the top-level directory inside the archive.
 */
export function releaseArchiveBase(
  platform: string = process.platform,
  arch: string = process.arch,
): string {
  return `xbintsc-${platform}-${arch}`;
}

/**
 * Extension for a release archive. `.tar.zst` when a `zstd` binary is available
 * (smaller, faster), otherwise the universally supported `.tar.gz`.
 */
export function releaseArchiveExtension(useZstd: boolean): string {
  return useZstd ? ".tar.zst" : ".tar.gz";
}

/**
 * Directory that may hold a toolchain shipped next to the package:
 * `<package>/vendor/<os>-<arch>` (or a generic `vendor/`). Undefined when none
 * is present, in which case the driver falls back to the system PATH.
 */
export function findVendorDir(): string | undefined {
  const roots: string[] = [];
  try {
    roots.push(findPackageRoot());
  } catch {
    // `runtime/` may be absent (e.g. a bare `emit` install); vendor is optional.
  }
  roots.push(resolve(here, "../../.."), resolve(here, "../.."), process.cwd());
  for (const root of roots) {
    const specific = join(root, "vendor", platformSlug());
    if (existsSync(specific)) return specific;
    const generic = join(root, "vendor");
    if (existsSync(generic)) return generic;
  }
  return undefined;
}
