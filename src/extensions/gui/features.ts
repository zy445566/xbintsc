/**
 * Optional link features of the `gui` extension.
 *
 * The GUI engine itself is always linked with `--ext gui`; these are the two
 * heavy halves a program may never touch:
 *
 *   - `media` — the FFmpeg decoder backend (`runtime/ext_media/`, `media.a`).
 *     The DOM media surface lives in `gui.a`; only decoding is optional, and an
 *     element without the backend still exists and fires `error`.
 *   - `gpu` — glslang (+ SPIRV-Cross on macOS), the GLSL→SPIR-V translator used
 *     by WebGL *and* by the `mix-blend-mode` offscreen compositor. The engine's
 *     own shaders are precompiled, so a DOM/2D-canvas page never needs it.
 *
 * Detection is static: the compiler sees imported `.html`/`.css` assets and all
 * bundled modules, so it can recognise `<video>`/`<audio>`, `getContext("webgl")`
 * and `mix-blend-mode`. When markup can be built at run time the usage collector
 * sets `dynamicMarkup`, which conservatively enables both features; users who
 * know better can shrink the binary with `--no-feature`.
 */

import { join } from "node:path";
import type { LinkFeature, ProgramUsage } from "../link-feature.js";
import { findRuntimeDir, platformSlug } from "../../driver/paths.js";
import { findRuntimeLibrary, runtimeLibDir } from "../../driver/runtime-lib.js";

/** `<video …>` / `<audio …>` in an HTML string or asset. */
const MEDIA_TAG = /<(?:video|audio)\b/i;
/** A literal tag name handed to `createElement("video")`. */
const MEDIA_TAG_NAME = /^(?:video|audio)$/i;
/** Anything that needs the GLSL translator. */
const GPU_TOKEN = /webgl|mix-blend-mode|mixBlendMode/i;

/** True when the program may create or drive a media element. */
export function hasMediaUsage(usage: ProgramUsage): boolean {
  if (usage.dynamicMarkup) return true;
  for (const name of ["Audio", "HTMLMediaElement", "HTMLVideoElement", "HTMLAudioElement", "decodeAudioData"]) {
    if (usage.identifiers.has(name)) return true;
  }
  return usage.strings.some((value) => MEDIA_TAG.test(value) || MEDIA_TAG_NAME.test(value));
}

/** True when the program may compile GLSL at run time (WebGL or blending). */
export function hasGpuUsage(usage: ProgramUsage): boolean {
  if (usage.dynamicMarkup) return true;
  if (usage.identifiers.has("mixBlendMode")) return true;
  if (usage.getContext.some((kind) => kind === undefined || /webgl/i.test(kind))) return true;
  return usage.strings.some((value) => GPU_TOKEN.test(value));
}

/** True when the program may ask for a WebGL context (not just blending). */
export function hasWebglUsage(usage: ProgramUsage): boolean {
  if (usage.dynamicMarkup) return true;
  if (usage.getContext.some((kind) => kind === undefined || /webgl/i.test(kind))) return true;
  return usage.strings.some((value) => /webgl/i.test(value));
}

/** True when the program may draw a bitmap or SVG. */
export function hasImageUsage(usage: ProgramUsage): boolean {
  if (usage.dynamicMarkup) return true;
  for (const name of ["Image", "ImageBitmap", "createImageBitmap", "drawImage"]) {
    if (usage.identifiers.has(name)) return true;
  }
  return usage.strings.some((value) =>
    /<img\b|<svg\b|background-image|url\(|\.svg\b|drawImage|createImageBitmap/i.test(value),
  );
}

/** True when the program may need full OpenType shaping (non-Latin text). */
export function hasShapingUsage(usage: ProgramUsage): boolean {
  if (usage.dynamicMarkup) return true;
  return usage.strings.some((value) => {
    for (const character of value) {
      if ((character.codePointAt(0) ?? 0) > 0x7f) return true;
    }
    return /\\u\{[0-9a-fA-F]+\}|\\u[0-9a-fA-F]{4}|&#(?:x[0-9a-fA-F]+|\d+);/.test(value);
  });
}

/** Absolute path to the per-platform prebuilt media archive. */
function mediaArchive(): string {
  return join(runtimeLibDir(), process.platform === "win32" ? "media.lib" : "media.a");
}

export const guiLinkFeatures: readonly LinkFeature[] = [
  {
    name: "media",
    description: "FFmpeg decoding for <video>/<audio>",
    /**
     * The registration bridge is a tiny C file compiled and linked only when
     * the feature is active. Its constructor installs the decoder table into
     * the engine and references the backend entry points, which is what makes
     * the linker extract `media.a` — an archive whose only entry point is a
     * constructor would otherwise never be pulled in.
     */
    runtimeSources: () => [join(findRuntimeDir(), "ext_media", "media_bridge.c")],
    nativeObjects: () => {
      const archive = findRuntimeLibrary("media");
      if (archive === undefined) {
        throw new Error(
          `media native library not found at ${mediaArchive()} (media.a or media.lib).\n` +
            `The optional @xbintsc/${platformSlug()} package ships it: reinstall xbintsc ` +
            `without --omit=optional, or build it with \`npm run media\` (see doc/gui/build.md).`,
        );
      }
      return [archive];
    },
    /** `media.a` is C++ and FFmpeg itself needs libm; the GUI extension already
     * contributes the C++ runtime and the OS frameworks. On Windows the FFmpeg
     * objects reference the Win32 crypto/COM services (`bcrypt`, `ole32`) and
     * `user32`. */
    linkerFlags: () => (process.platform === "win32" ? ["-lbcrypt", "-lole32", "-luser32"] : ["-lm"]),
    used: hasMediaUsage,
  },
  {
    name: "gpu",
    description: "GLSL translation (WebGL and mix-blend-mode) via glslang",
    /**
     * `gpu_bridge.c` is linked only when the feature is active. It calls into
     * `gpu_feature.cpp` inside `gui.a`, which installs the shader-translation
     * table and references `compile_glsl`/`cross_compile_msl`; those references
     * are what pull the glslang members out of `gui.a`. Without the bridge the
     * engine's shader paths see a null table and fail gracefully (blend falls
     * back, WebGL reports a compile log).
     */
    runtimeSources: () => [join(findRuntimeDir(), "ext_gui", "gpu_bridge.c")],
    used: hasGpuUsage,
  },
  {
    name: "images",
    description: "stb_image/NanoSVG decoding for <img>, CSS backgrounds and inline <svg>",
    /**
     * `image_bridge.c` installs `XtImageApi` and references the decoders; a page
     * with no `<img>`/SVG/`background-image`/`drawImage` links neither
     * stb_image nor NanoSVG.
     */
    runtimeSources: () => [join(findRuntimeDir(), "ext_gui", "image_bridge.c")],
    used: hasImageUsage,
  },
  {
    name: "shaping",
    description: "HarfBuzz/FreeType shaping for complex scripts and CFF/OTF fonts",
    /**
     * `text_bridge.c` installs `TextApi` and references `text_hb.cpp`, pulling
     * HarfBuzz and FreeType out of `gui.a`. A Latin-only page keeps the faster
     * stb_truetype path and links neither library.
     */
    runtimeSources: () => [join(findRuntimeDir(), "ext_gui", "text_bridge.c")],
    used: hasShapingUsage,
  },
  {
    name: "webgl",
    description: "WebGL context and DOM API (requires gpu)",
    requires: ["gpu"],
    /**
     * `webgl_bridge.c` installs `XtWebglApi` and references the WebGL context
     * and DOM bindings, pulling them out of `gui.a`. A page that never asks for
     * WebGL therefore links none of `webgl_*`/`dom_api_webgl*`.
     */
    runtimeSources: () => [join(findRuntimeDir(), "ext_gui", "webgl_bridge.c")],
    used: hasWebglUsage,
  },
];
