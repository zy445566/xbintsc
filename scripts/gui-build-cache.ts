/**
 * Cache invalidation for the vendored GUI build trees.
 *
 * `build-gui.ts` reuses `vendor/*-build-lean` when the archive it produced is
 * already there, which is what makes a second `npm run gui` fast. CMake records
 * the compiler in its cache, but the macOS deployment target is only a cache
 * entry: a tree configured by an earlier checkout (or before the baseline was
 * pinned) keeps its old objects inside an archive that is supposed to target
 * the oldest supported macOS. This drops such a tree once, so the next
 * configure builds it for the current target.
 */

import { existsSync, readFileSync, rmSync } from "node:fs";
import { join } from "node:path";

/** Remove `buildDir` when its CMake cache targets a different macOS version. */
export function ensureDeploymentTarget(name: string, buildDir: string): void {
  if (process.platform !== "darwin") return;
  const cache = join(buildDir, "CMakeCache.txt");
  if (!existsSync(cache)) return;
  const configured = /^CMAKE_OSX_DEPLOYMENT_TARGET:STRING=(.*)$/m.exec(readFileSync(cache, "utf8"))?.[1]?.trim();
  const wanted = process.env.MACOSX_DEPLOYMENT_TARGET ?? "";
  if ((configured ?? "") === wanted) return;
  console.log(
    `xbintsc: ${name} was built for macOS '${configured ?? ""}'; rebuilding for '${wanted}'`,
  );
  rmSync(buildDir, { recursive: true, force: true });
}
