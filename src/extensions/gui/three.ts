/**
 * GUI extension: three.js compatibility renderer.
 *
 * `gui` has no WebGL backend, and three.js has no Canvas2D fallback, so a
 * three.js page compiled for the GUI extension dies at
 * `new WebGLRenderer(...)` with the library's own "Error creating WebGL
 * context". Patching a user's vendored three.js is not desirable, so the
 * compiler does it while bundling: this asset loader recognises the three.js
 * module (the `WebGLRenderer` / `WebGL1Renderer` class pair), renames the WebGL
 * implementation, and appends `runtime/ext_gui/three_compat.js` before the
 * module's export list. The export named `WebGLRenderer` then resolves to the
 * Canvas2D scene renderer while every other three.js export is untouched.
 *
 * The substitution happens for any three.js build with the same class shape
 * (r150-era and later ES module bundles), not just the copy used by one game.
 */

import { readFileSync } from "node:fs";
import { join } from "node:path";
import type { AssetLoadResult } from "../registry.js";
import { findRuntimeDir } from "../../driver/paths.js";

/** Absolute path of the Canvas2D implementation appended to three.js. */
function compatSourcePath(): string {
  return join(findRuntimeDir(), "ext_gui", "three_compat.js");
}

/** The three.js module's original WebGL renderer class declaration. */
const WEBGL_CLASS = /class\s+WebGLRenderer\s*\{/;
/** Its subclass, which must keep pointing at the renamed original. */
const WEBGL1_CLASS = /class\s+WebGL1Renderer\s+extends\s+WebGLRenderer\s*\{\s*\}/;

/**
 * Rewrite a three.js ES module so its `WebGLRenderer` export is the Canvas2D
 * fallback. Any other `.js` asset is returned verbatim.
 */
export function loadScriptAsset(path: string, source: string): AssetLoadResult {
  if (source.includes("__xtSwTAU")) return { moduleSource: source };
  if (!WEBGL_CLASS.test(source) || !WEBGL1_CLASS.test(source)) {
    return { moduleSource: source };
  }
  const compatPath = compatSourcePath();
  const compat = readFileSync(compatPath, "utf8");
  const renamed = source
    .replace(WEBGL_CLASS, "class WebGLRendererWebGL {")
    .replace(WEBGL1_CLASS, "class WebGL1Renderer extends WebGLRendererWebGL {}");
  /* The export list is the last `export {` in the bundled module. Insert the
   * replacement before it so the exported binding is initialised in order. */
  const at = renamed.lastIndexOf("\nexport {");
  const moduleSource =
    at < 0
      ? `${renamed}\n${compat}\n`
      : `${renamed.slice(0, at)}\n${compat}\n${renamed.slice(at)}`;
  return { moduleSource, dependencies: [compatPath] };
}
