/**
 * GUI extension: a self-contained, GPU-accelerated HTML/CSS renderer.
 *
 * Unlike the system-WebView approach, this extension ships its *own* rendering
 * engine (HTML parser, CSS cascade, layout, GPU compositor) written in C/C++.
 * It is therefore delivered as a per-platform prebuilt archive — the same way
 * the core runtime ships `runtime/lib/<os>-<arch>/core.a` — and linked through
 * `nativeObjects`. The core compiler stays platform-agnostic and never learns
 * that "gui" exists beyond the module bindings below.
 *
 * Design and roadmap live in `doc/gui.md`. The C++/GPU sources are not built
 * yet: `nativeObjects()` fails with an actionable message until
 * `runtime/lib/<os>-<arch>/gui.a` exists.
 *
 * TypeScript sees one importable module:
 *
 *     import { createWindow, run } from "gui";
 *
 * Window instances are runtime objects whose methods are dispatched in C
 * (through the existing `xt_call_method` object model); the registry only
 * needs to bind the top-level functions.
 */

import { join } from "node:path";
import type { Extension, ExtensionModule } from "../registry.js";
import { platformSlug } from "../../driver/paths.js";
import { findRuntimeLibrary, runtimeLibDir } from "../../driver/runtime-lib.js";
import { loadHtmlAsset } from "./html.js";
import { loadScriptAsset } from "./three.js";

/** Top-level `gui` module bindings. */
const guiModule: ExtensionModule = {
  namespace: "gui",
  exports: {
    createWindow: { symbol: "xt_gui_create_window" },
    run: { symbol: "xt_gui_run" },
    quit: { symbol: "xt_gui_quit" },
    /** Window handles are opaque runtime objects created by `createWindow`. */
  },
};

/**
 * Browser globals a script written for a real page expects to find.
 *
 * Every entry is a *value*, not a function: reading the identifier calls a
 * runtime getter that returns the live host object, so `document.getElementById`
 * or `new Image()` work without a `declare` in the source. The host objects are
 * installed on the current window by `runtime/ext_gui/browser.cpp`.
 *
 * `xt_browser_type` carries the name as its argument, which is how one getter
 * serves the whole family of DOM types, and `xt_browser_missing` is deliberately
 * `undefined` so feature probes such as `typeof OffscreenCanvas === "undefined"`
 * take the fallback path instead of calling a stub that does nothing.
 */
const hostTypes = [
  "Image",
  "Audio",
  "AudioContext",
  "webkitAudioContext",
  "OffscreenCanvas",
  "ImageBitmap",
  "ImageData",
  "HTMLCanvasElement",
  "Path2D",
  "HTMLImageElement",
  "HTMLVideoElement",
  "HTMLAudioElement",
  "HTMLElement",
  "Element",
  "Node",
  "Event",
  "CustomEvent",
  "ProgressEvent",
  "UIEvent",
  "MouseEvent",
  "PointerEvent",
  "WheelEvent",
  "KeyboardEvent",
  "FocusEvent",
  "InputEvent",
  "SubmitEvent",
  "AbortController",
  "AbortSignal",
  "EventTarget",
  "DOMParser",
  "Request",
  "Headers",
  "Response",
  "ReadableStream",
  "WebGLRenderingContext",
  "WebGL2RenderingContext",
  "WebGLBuffer",
  "WebGLTexture",
  "WebGLShader",
  "WebGLProgram",
  "WebGLFramebuffer",
  "WebGLRenderbuffer",
  "WebGLUniformLocation",
  "WebGLActiveInfo",
  "WebGLVertexArrayObject",
  "WebGLSampler",
  "WebGLQuery",
  "WebGLSync",
  "WebGLTransformFeedback",
  "XRWebGLLayer",
  "XRWebGLBinding",
] as const;

const browserGlobals: Record<string, { symbol: string; argument?: string }> = {
  window: { symbol: "xt_browser_window" },
  self: { symbol: "xt_browser_window" },
  document: { symbol: "xt_browser_document" },
  navigator: { symbol: "xt_browser_navigator" },
  localStorage: { symbol: "xt_browser_local_storage" },
  __THREE_DEVTOOLS__: { symbol: "xt_browser_missing" },
  createImageBitmap: { symbol: "xt_browser_create_image_bitmap" },
};
for (const type of hostTypes) {
  browserGlobals[type] = { symbol: "xt_browser_type", argument: type };
}

/** Absolute path to the per-platform prebuilt GUI archive (`gui.a`, or
 * `gui.lib` with the MSVC ABI on Windows). */
function guiArchive(): string {
  return join(runtimeLibDir(), "gui.a");
}

export const guiExtension: Extension = {
  name: "gui",
  description: "GPU-accelerated HTML/CSS GUI (self-hosted renderer, no webview)",
  nativeObjects: () => {
    const archive = findRuntimeLibrary("gui");
    if (archive === undefined) {
      throw new Error(
        `GUI native library not found at ${guiArchive()} (or gui.lib).\n` +
          `The optional @xbintsc/${platformSlug()} package ships it: reinstall xbintsc ` +
          `without --omit=optional, or build it from a source checkout (see doc/gui.md).`,
      );
    }
    return [archive];
  },
  /**
   * System libraries the archive itself depends on. The vendored third-party
   * libraries (GPU backend, HarfBuzz, FreeType, ...) are linked *into* `gui.a`
   * by its build script, so only OS frameworks remain here. TODO(gui): confirm
   * the exact set once the GPU backend is chosen (see doc/gui.md).
   */
  linkerFlags: () => {
    if (process.platform === "darwin") {
      return [
        "-framework", "CoreMedia",
        "-framework", "CoreVideo",
        "-framework", "Cocoa",
        "-weak_framework", "UniformTypeIdentifiers",
        "-framework", "IOKit",
        "-framework", "ForceFeedback",
        "-framework", "Carbon",
        "-framework", "CoreAudio",
        "-framework", "AudioToolbox",
        "-framework", "AVFoundation",
        "-framework", "Foundation",
        "-framework", "GameController",
        "-framework", "Metal",
        "-framework", "QuartzCore",
        /* HarfBuzz's CoreText shaper (macOS system fonts). */
        "-framework", "CoreText",
        "-framework", "CoreGraphics",
        "-framework", "CoreFoundation",
        "-weak_framework", "CoreHaptics",
        "-lpthread",
        "-lm",
        "-lc++",
      ];
    }
    if (process.platform === "win32") {
      return [
        /* SDL_GPU D3D12 backend. */
        "-ld3d12", "-ldxgi", "-ldxguid",
        /* Win32 / COM / multimedia services SDL3 uses. */
        "-luser32", "-lgdi32", "-lshell32",
        "-lole32", "-loleaut32", "-luuid", "-ladvapi32",
        "-lwinmm", "-limm32", "-lversion", "-lsetupapi",
        /* The engine is C++, but `msvcprt` (the DLL C++ runtime) must not be
         * named here: `gui.lib` is built with `CMAKE_MSVC_RUNTIME_LIBRARY=
         * MultiThreaded` and every engine object records `libcpmt` (the static
         * C++ runtime) as a dependent library — see `scripts/build-gui.ts`.
         * The two runtimes define the same symbols, so requesting the DLL one
         * as well fails the link with `LNK2038: mismatch detected for
         * 'RuntimeLibrary'` plus a wall of `LNK2005: already defined` errors
         * (surfacing as `clang: linker command failed with exit code 1169`).
         * clang links the static `libcpmt` by default, which is the CRT the
         * archive and the xbintsc runtime objects were both built against. */
      ];
    }
    return ["-lX11", "-lwayland-client", "-lEGL", "-lGL", "-ldl", "-lpthread", "-lstdc++"];
  },
  modules: () => ({ gui: guiModule }),
  /**
   * `__registerScript(id, fn)` is emitted by the `.html` asset loader and
   * called at module initialization; it records the AOT-compiled `<script>`
   * body so the engine can run it after parsing a document.
   */
  builtins: () => ({
    __registerScript: { symbol: "xt_register_script" },
    __xt_defer: { symbol: "xt_gui_defer_module" },
    requestAnimationFrame: { symbol: "xt_browser_request_animation_frame" },
    cancelAnimationFrame: { symbol: "xt_browser_cancel_animation_frame" },
    matchMedia: { symbol: "xt_browser_match_media" },
    getComputedStyle: { symbol: "xt_browser_computed_style" },
    escape: { symbol: "xt_browser_escape" },
    alert: { symbol: "xt_browser_alert" },
    prompt: { symbol: "xt_browser_prompt" },
    confirm: { symbol: "xt_browser_confirm_global" },
  }),
  /** Browser globals (`window`, `document`, the DOM type names). */
  globalValues: () => browserGlobals,
  /** Rewrite imported `.html` assets and patch three.js's shader unroller. */
  assetLoaders: () => ({ ".html": loadHtmlAsset, ".js": loadScriptAsset, ".mjs": loadScriptAsset }),
};
