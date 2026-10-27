/**
 * Media extension: `<video>` / `<audio>` decoding for the GUI engine.
 *
 * The GUI extension is deliberately decoder-free: it models media elements,
 * renders their frames and routes the DOM API, but keeps FFmpeg out of `gui.a`
 * so a program that only needs windows and HTML/CSS does not pay for a media
 * stack it never uses. This extension supplies the decoding half:
 *
 *   - `media.a` (`media.lib` with the MSVC ABI on Windows, built by
 *     `npm run media`) contains the FFmpeg-linked player from
 *     `runtime/ext_media/media.cpp` and the static FFmpeg libraries;
 *   - `runtime/ext_media/media_bridge.c` is compiled and linked directly. Its
 *     constructor installs the player table into the GUI engine and references
 *     the backend entry points, which makes the linker extract the archive
 *     member — a static archive whose only entry point is a constructor would
 *     otherwise never be pulled in.
 *
 * There is no importable TypeScript module: the feature is the DOM media
 * surface that the `gui` module already exposes. Enable both extensions:
 *
 *     xbintsc run app.ts --ext gui,media
 *
 * The archive is built for every host platform. On Windows FFmpeg is compiled
 * with the MSVC-targeting clang-cl so its COFF objects and static CRT match
 * `gui.lib`; see `doc/gui/build.md`.
 */

import { join } from "node:path";
import type { Extension } from "../registry.js";
import { findRuntimeDir, platformSlug } from "../../driver/paths.js";
import { findRuntimeLibrary, runtimeLibDir } from "../../driver/runtime-lib.js";

/** Absolute path to the per-platform prebuilt media archive. */
function mediaArchive(): string {
  return join(runtimeLibDir(), process.platform === "win32" ? "media.lib" : "media.a");
}

export const mediaExtension: Extension = {
  name: "media",
  description: "Video/audio decoding for <video>/<audio> (requires gui, links FFmpeg)",
  requires: ["gui"],
  /**
   * The registration bridge is a tiny C file linked as a normal object, so its
   * constructor always runs. Keeping it out of `media.a` is what guarantees the
   * archive is searched (see the header comment).
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
  linkerFlags: () =>
    process.platform === "win32" ? ["-lbcrypt", "-lole32", "-luser32"] : ["-lm"],
};
