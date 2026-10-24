/**
 * Host-independent helpers for the test suite.
 *
 * The suite follows one rule: repository text and compiler artifacts are
 * canonical (LF newlines, `/` separators), while output a user sees may stay
 * native — a stack frame on Windows legitimately says `C:\dir\file.ts`, and Node
 * prints it the same way. These helpers mark that boundary. Do not run them
 * over arbitrary program output: a backslash there can be a real character (an
 * escape sequence, JSON, or a Windows path the program printed on purpose).
 */

import { readFileSync } from "node:fs";

/** Canonical newlines: CRLF and a lone CR both become LF. */
export function normalizeEol(text: string): string {
  return text.replace(/\r\n?/g, "\n");
}

/**
 * Read a repository text file as canonical LF text.
 *
 * `.gitattributes` checks every text file out with LF, but a stale checkout or
 * an editor can still hand a test CRLF. Reading through this helper keeps
 * generated-document comparisons (the support matrix), documentation structure
 * checks and fixture copies independent of that.
 */
export function readTextFile(file: string | URL): string {
  return normalizeEol(readFileSync(file, "utf8"));
}

/**
 * Render a native path the way the compiler writes it into host-independent
 * artifacts: LLVM IR, cache manifests, fingerprints and the `path` module use
 * `/` on every platform. Apply this to the *input* of an assertion (a temp
 * directory, `resolve(...)`, an `import.meta.url` path), not to program output
 * that may legitimately contain a backslash.
 *
 * When the expected value is inherently a path, prefer building it with
 * `node:path` (`join`, `resolve`, `relative`) over hardcoding either separator.
 */
export function toPosixPath(path: string): string {
  return path.replace(/\\/g, "/");
}
