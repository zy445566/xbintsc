/**
 * Detecting a prebuilt runtime archive that this machine's toolchain cannot
 * link.
 *
 * The optional `@xbintsc/<os>-<arch>` package carries archives compiled on the
 * release runner (`core.a`, `ext_node.a`, `gui.a`, `media.a`). Its Xcode
 * Command Line Tools or Visual Studio C++ toolset can be **newer** than the
 * one on the machine that runs the build: a linker can then reject the archive
 * with unresolved symbols (`LNK2038`/`LNK2019`, `Undefined symbols`,
 * `undefined reference`) or `was built for newer macOS version`.
 *
 * The driver answers that with one automatic retry that compiles the runtime
 * from the sources shipped in the npm package. Its decision is based on the
 * linker's own output rather than a version comparison, because an archive does
 * not record the toolset that produced it portably: Mach-O objects carry
 * `LC_BUILD_VERSION`, COFF objects do not carry an MSVC toolset version.
 *
 * `gui.a`/`media.a` are exempt: they bundle SDL3/FFmpeg/glslang, whose sources
 * are not part of the npm package, so they cannot be rebuilt from it.
 */

import { dirname, resolve } from "node:path";

const ARCHIVE_SUFFIXES = [".a", ".lib"] as const;

/** True when `candidate` is a static archive that sits in one of `dirs`. */
export function isPrebuiltArchivePath(candidate: string, dirs: readonly string[]): boolean {
  if (!ARCHIVE_SUFFIXES.some((suffix) => candidate.endsWith(suffix))) return false;
  const parent = dirname(resolve(candidate));
  return dirs.some((dir) => resolve(dir) === parent);
}

/**
 * Linker messages that mean "these objects were produced by a different
 * toolchain/SDK". Kept deliberately narrow: a missing file, a bad flag or a
 * permission error must not trigger the (slow) source rebuild.
 */
const MISMATCH_PATTERNS = [
  /LNK2038/i, // mismatch detected for 'RuntimeLibrary'/'_MSC_VER'
  /LNK20(?:01|19)/i, // unresolved external symbol
  /undefined symbols?/i, // ld64
  /undefined reference/i, // GNU/LLD
  /symbol\(s\) not found/i, // ld64 summary line
  /was built for newer (?:macOS|iOS|tvOS|watchOS|visionOS)/i, // ld64 warning
  /but linking in object file built for/i, // ld64 deployment-target error
  /has an incompatible (?:architecture|platform)/i,
] as const;

/** Whether a failed link's stderr looks like a toolchain/archive mismatch. */
export function looksLikePrebuiltLinkFailure(stderr: string): boolean {
  return MISMATCH_PATTERNS.some((pattern) => pattern.test(stderr));
}

/** First non-empty line of a command's stderr, for a one-line build notice. */
export function firstFailureLine(stderr: string): string {
  for (const line of stderr.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (trimmed.length > 0) return trimmed.length > 160 ? `${trimmed.slice(0, 157)}...` : trimmed;
  }
  return "";
}
