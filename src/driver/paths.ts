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
    resolve(process.cwd(), "runtime"),
  ];
  for (const candidate of candidates) {
    if (existsSync(join(candidate, "rt.h"))) return candidate;
  }
  throw new Error("Unable to locate the xtsc runtime directory (missing runtime/rt.h)");
}

export function findPackageRoot(): string {
  return resolve(findRuntimeDir(), "..");
}
