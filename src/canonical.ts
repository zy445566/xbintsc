/**
 * Canonical bytes for compiler-emitted artifacts.
 *
 * The compiler runs on Windows, macOS and Linux, and can be compiled by
 * itself. Anything it writes into a byte-compared artifact — LLVM IR, debug
 * metadata, the compiler fingerprint, the bundled module text and the `path`
 * module's results — has to use the same representation on every host. These
 * constants are that representation: do not spell `"/"` or `"\n"` in code that
 * emits an artifact.
 *
 * Native paths are deliberately different. Diagnostics, `import.meta.filename`
 * and `process.cwd()` keep the host separator, exactly like Node. Convert a
 * native path with {@link artifactPath} only when it enters an artifact.
 */

/** Separator between path segments in a compiler artifact. Deliberately not
 *  selected by `process.platform`: every host writes `/`, so the same source
 *  produces the same bytes (and Windows accepts `/` in every API). */
export const ARTIFACT_PATH_SEPARATOR = "/";

/** Line ending between records in a compiler artifact. Deliberately not
 *  `os.EOL`/`"\r\n"` on Windows: the runtime opens stdout/stderr in binary
 *  mode, and CRLF would make IR, fingerprints and documentation differ per
 *  host. */
export const ARTIFACT_LINE_ENDING = "\n";

/**
 * Rewrite a native path for an artifact: `C:\dir\file.ts` becomes
 * `C:/dir/file.ts`. A backslash is always a separator here, matching how the
 * runtime and Node treat Windows paths.
 */
export function artifactPath(path: string): string {
  return path.replaceAll("\\", ARTIFACT_PATH_SEPARATOR);
}
