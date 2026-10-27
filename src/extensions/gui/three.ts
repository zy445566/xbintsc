/**
 * GUI extension: three.js shader-unroller compatibility.
 *
 * three.js's `WebGLRenderer` runs unchanged on this engine's real WebGL 2
 * backend; no Canvas2D fallback (and no renderer wrapper) is needed. The one
 * shader-side workaround is its `unrollLoops` pass: its lazy global regex does
 * not behave reliably on this runtime. The shader rewriter expands
 * `#pragma unroll_loop_start` itself (and substitutes `UNROLLED_LOOP_INDEX`),
 * so disabling the JavaScript pass leaves valid GLSL instead of a
 * half-expanded loop.
 */

import type { AssetLoadResult } from "../registry.js";

/** three.js's unroll-loop regex declaration (r150–r16x naming). */
const LOOP_PATTERN = /const unrollLoopPattern = [^\n]*;/;

/**
 * Patch a three.js ES module so its shaders keep their light loops. Any other
 * `.js` asset is returned verbatim.
 */
export function loadScriptAsset(path: string, source: string): AssetLoadResult {
  return {
    moduleSource: source.replace(LOOP_PATTERN, "const unrollLoopPattern = /__xt_never_match__/;"),
  };
}
