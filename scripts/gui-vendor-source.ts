/**
 * Fetch a pinned release tarball into `vendor/` and extract it under a stable
 * directory name.
 *
 * Split out of `build-gui.ts` (which has a file-size budget) because the same
 * "fetch, extract, rename" step is repeated for every vendored build dependency
 * (SDL3 is a git clone, but FreeType, HarfBuzz, glslang, SPIRV-Cross, Brotli and
 * the woff2 decoder all arrive this way).
 *
 * Release tarballs are used rather than git clones because they ship generated
 * sources, so no ragel/autotools step is needed. `marker` is a file that only
 * exists once the tarball has been extracted (e.g. `CMakeLists.txt`), which is
 * what makes a cached extraction reusable.
 */

import { existsSync, mkdirSync, renameSync, rmSync } from "node:fs";
import { join } from "node:path";

export interface VendorFetchContext {
  /** Root `vendor/` directory, created on demand. */
  readonly vendorDir: string;
  /** Run a command (curl/tar), failing the build on a non-zero exit. */
  run(command: string, args: readonly string[]): void;
  /** Abort the build with a one-line message. */
  fail(message: string): never;
}

/** Download `url` into `vendor/` and extract it under `name`, returning the
 * extracted directory (an already-extracted copy is reused). */
export function ensureTarballSource(
  context: VendorFetchContext,
  name: string,
  url: string,
  extractedName: string,
  marker = "CMakeLists.txt",
): string {
  const { vendorDir, run, fail } = context;
  const destination = join(vendorDir, name);
  if (existsSync(join(destination, marker))) return destination;
  /* Keep the URL's compression suffix: `tar -xf` sniffs the format anyway, but a
   * misleading name makes a failed extraction harder to diagnose. */
  const tarball = join(vendorDir, `${name}${url.endsWith(".tar.gz") ? ".tar.gz" : ".tar.xz"}`);
  console.log(`xbintsc: fetching ${name} from ${url}`);
  mkdirSync(vendorDir, { recursive: true });
  run("curl", ["-sL", "--fail", "-o", tarball, url]);
  run("tar", ["-xf", tarball, "-C", vendorDir]);
  const extracted = join(vendorDir, extractedName);
  if (!existsSync(extracted)) fail(`expected ${extractedName} after extracting ${name}`);
  rmSync(destination, { recursive: true, force: true });
  renameSync(extracted, destination);
  rmSync(tarball, { force: true });
  return destination;
}
