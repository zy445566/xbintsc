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

/**
 * Directory holding the third-party sources fetched by `npm run vendor`
 * (`vendor/` next to `runtime/`).
 *
 * They are not committed: the tree is rebuilt from pinned upstream releases by
 * `scripts/fetch-vendored.ts`. `runtime/xt_storage` and the `compression`
 * feature resolve their C sources here, so a build that needs them fails with
 * the command to run when the directory is missing.
 */
export function findVendorDir(): string {
  const candidate = resolve(findRuntimeDir(), "..", "vendor");
  const markers = [join("sqlite3", "sqlite3.c"), join("miniz", "miniz.c")];
  if (markers.some((marker) => existsSync(join(candidate, marker)))) return candidate;
  throw new Error(
    "Unable to locate the xbintsc vendor directory (missing vendor/miniz/miniz.c or " +
      "vendor/sqlite3/sqlite3.c); run `npm run vendor` in a source checkout, or reinstall " +
      "xbintsc so the package's vendored sources are present.",
  );
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
 * Base name (no extension) of the published release archive, e.g.
 * `xbintsc-0.3.8-linux-x64`. The version is embedded so downloads from
 * different releases can coexist and users can tell them apart. The top-level
 * directory inside the archive still uses {@link releaseArchiveBase}.
 */
export function releaseArchiveFileName(
  version: string,
  platform: string = process.platform,
  arch: string = process.arch,
): string {
  return `xbintsc-${version}-${platform}-${arch}`;
}

/**
 * Extension for a release archive. `.tar.zst` when a `zstd` binary is available
 * (smaller, faster), otherwise the universally supported `.tar.gz`.
 */
export function releaseArchiveExtension(useZstd: boolean): string {
  return useZstd ? ".tar.zst" : ".tar.gz";
}
