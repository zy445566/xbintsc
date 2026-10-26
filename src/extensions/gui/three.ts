/**
 * GUI extension: three.js shader-unroller compatibility.
 *
 * three.js's `WebGLRenderer` runs unchanged on this engine's real WebGL 2
 * backend; no Canvas2D fallback (and no renderer wrapper) is needed. The one
 * shader-side workaround is its `unrollLoops` pass: the replacement regex uses
 * `\s` inside a character class, which the runtime regexp engine (POSIX ERE)
 * does not understand, so `hemisphereLights[i]` stayed dynamic and GLSL rejects
 * a variable sampler-array index. Rewrite that one character class to explicit
 * whitespace; the lazy loop pattern itself works.
 */

import type { AssetLoadResult } from "../registry.js";

/** three.js's index replacement regex (`loopReplacer`). */
const INDEX_PATTERN = ".replace( /\\[\\s*i\\s*\\]/g, '[ ' + i + ' ]' )";
/** The same regex with a POSIX-ERE-friendly whitespace class. */
const INDEX_PATTERN_ERE = ".replace( /\\[[ \\t\\r\\n]*i[ \\t\\r\\n]*\\]/g, '[ ' + i + ' ]' )";

/** Patch a three.js ES module so its shader loop unroller can run here. */
export function loadScriptAsset(path: string, source: string): AssetLoadResult {
  return { moduleSource: source.replace(INDEX_PATTERN, INDEX_PATTERN_ERE) };
}
