/**
 * GUI extension: three.js compatibility renderer.
 *
 * three.js has no Canvas2D fallback, so a three.js page compiled for the GUI
 * extension used to die at `new WebGLRenderer(...)` when no WebGL context was
 * available. Patching a user's vendored three.js is not desirable, so the
 * compiler does it while bundling: this asset loader recognises the three.js
 * module (the `WebGLRenderer` / `WebGL1Renderer` class pair), renames the WebGL
 * implementation and the bundled Canvas2D renderer, and appends a small wrapper
 * before the module's export list.
 *
 * The wrapper probes for a WebGL context on first construction. With the gui
 * extension's real WebGL backend the page gets three.js's own renderer; only a
 * page without a WebGL context (a failed device, a platform whose SDL_GPU
 * backend cannot consume SPIR-V) falls back to Canvas2D.
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
/** The Canvas2D renderer's class declaration inside `three_compat.js`. */
const COMPAT_CLASS = /class\s+WebGLRenderer\s*\{/;

/**
 * The wrapper appended after the Canvas2D renderer. It keeps three.js's own
 * `WebGLRenderer` when the engine has a WebGL context and returns the Canvas2D
 * renderer otherwise (a constructor may return a different object).
 */
const WRAPPER = `
let __xtWebGLAvailable = null;
function __xtHasWebGL() {
  if (__xtWebGLAvailable !== null) return __xtWebGLAvailable;
  let available = false;
  try {
    const probe = document.createElement("canvas");
    available = probe.getContext("webgl") != null;
  } catch (error) {
    available = false;
  }
  __xtWebGLAvailable = available;
  return available;
}
class WebGLRenderer extends WebGLRendererWebGL {
  constructor(parameters) {
    if (__xtHasWebGL()) {
      super(parameters);
      return;
    }
    return new __XtCanvas2DRenderer(parameters);
  }
}
class WebGL1Renderer extends WebGLRenderer {}
`;

/**
 * Rewrite a three.js ES module so its `WebGLRenderer` export prefers the real
 * WebGL implementation and falls back to Canvas2D. Any other `.js` asset is
 * returned verbatim.
 */
export function loadScriptAsset(path: string, source: string): AssetLoadResult {
  if (source.includes("__xtSwTAU")) return { moduleSource: source };
  if (!WEBGL_CLASS.test(source) || !WEBGL1_CLASS.test(source)) {
    return { moduleSource: source };
  }
  const compatPath = compatSourcePath();
  const compatSource = readFileSync(compatPath, "utf8");
  // The fallback renderer gets a private name; the public `WebGLRenderer` is
  // the wrapper defined below.
  const compat = compatSource.replace(COMPAT_CLASS, "class __XtCanvas2DRenderer {");
  const renamed = source
    .replace(WEBGL_CLASS, "class WebGLRendererWebGL {")
    .replace(WEBGL1_CLASS, "class WebGL1RendererWebGL extends WebGLRendererWebGL {}");
  /* The export list is the last `export {` in the bundled module. Insert the
   * replacement and the wrapper before it so the exported bindings are
   * initialised in order. */
  const at = renamed.lastIndexOf("\nexport {");
  const block = `${compat}\n${WRAPPER}\n`;
  const moduleSource =
    at < 0 ? `${renamed}\n${block}\n` : `${renamed.slice(0, at)}\n${block}${renamed.slice(at)}`;
  return { moduleSource, dependencies: [compatPath] };
}
