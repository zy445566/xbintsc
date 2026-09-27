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

import { existsSync } from "node:fs";
import { join } from "node:path";
import type { Extension, ExtensionModule } from "../registry.js";
import { findRuntimeDir, platformSlug } from "../../driver/paths.js";

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

/** Absolute path to the per-platform prebuilt GUI archive. */
function guiArchive(): string {
  return join(findRuntimeDir(), "lib", platformSlug(), "gui.a");
}

export const guiExtension: Extension = {
  name: "gui",
  description: "GPU-accelerated HTML/CSS GUI (self-hosted renderer, no webview)",
  nativeObjects: () => {
    const archive = guiArchive();
    if (!existsSync(archive)) {
      throw new Error(
        `GUI native library not found at ${archive}.\n` +
          `Build it for ${platformSlug()} first (see doc/gui.md).`,
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
        "-weak_framework", "CoreHaptics",
        "-lpthread",
        "-lm",
        "-lc++",
      ];
    }
    if (process.platform === "win32") {
      return [
        "-ld3d12", "-ldxgi", "-ldxguid",
        "-luser32", "-lgdi32", "-lshell32",
        "-lole32", "-loleaut32", "-luuid", "-ladvapi32",
        "-lstdc++",
      ];
    }
    return ["-lX11", "-lwayland-client", "-lEGL", "-lGL", "-ldl", "-lpthread", "-lstdc++"];
  },
  modules: () => ({ gui: guiModule }),
};
