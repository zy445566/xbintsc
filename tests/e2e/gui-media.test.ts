/**
 * End-to-end tests for `<video>` / `<audio>` support.
 *
 * These exercise the real decoding path (the `media` extension's vendored
 * FFmpeg + the GUI's SDL audio sink) and the DOM media API. They are skipped
 * unless both the `gui` and `media` archives exist: the `gui` extension alone
 * is deliberately decoder-free. The fixtures live in `examples/resources`; when
 * they are absent (a trim checkout) the suite is skipped rather than failing.
 */

import { existsSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { guiExtension } from "../../src/extensions/gui/index.js";
import { mediaExtension } from "../../src/extensions/media/index.js";
import { findRuntimeLibrary } from "../../src/driver/runtime-lib.js";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";
import { readTextFile, toPosixPath } from "../platform.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
const resourceDir = join(root, "examples", "resources");
const moviePath = join(resourceDir, "movie.mp4");
const soundPath = join(resourceDir, "sounds.mp3");
const exampleDir = join(root, "examples", "gui");

/** A local path safe to embed in an HTML attribute (no percent-encoding: the
 * media loader hands the string straight to FFmpeg). */
function mediaSrc(absolutePath: string): string {
  return toPosixPath(absolutePath);
}

const resourcesAvailable = existsSync(moviePath) && existsSync(soundPath);

/**
 * The `media` archive is built separately (`npm run media`) and is POSIX-only
 * for now. Without it the suite would compile GUI programs that report the
 * expected `error` events, i.e. it would test the wrong thing.
 */
const mediaAvailable = process.platform !== "win32" && findRuntimeLibrary("media") !== undefined;

describe.skipIf(!guiAvailable || !resourcesAvailable || !mediaAvailable)("gui + media extensions", () => {
  const harness = createGuiHarness();
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("decodes video/audio and drives playback through the DOM API", () => {
    const movie = mediaSrc(moviePath);
    const sound = mediaSrc(soundPath);
    const { value, stdout } = harness.compileAndRun(
      "media",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "media", width: 400, height: 300 });
      win.on("ready", () => {
        win.loadHTML(\`<html><body style="margin:0">
          <video id="v" src="${movie}" autoplay muted></video>
          <audio id="a" src="${sound}"></audio>
        </body></html>\`);

        const v: any = win.document.getElementById("v");
        const a: any = win.document.getElementById("a");
        let ticks = 0;

        v.addEventListener("loadedmetadata", () => {
          console.log("duration=" + (v.duration > 0) + ":" + (a.duration > 0));
          console.log("dims=" + (v.videoWidth > 0) + ":" + (v.videoHeight > 0));
          console.log("ready=" + v.readyState);
          v.pause();
          console.log("paused=" + v.paused);
          v.currentTime = 2.5;
          console.log("seek=" + (v.currentTime >= 2.4));
          v.volume = 0.25;
          console.log("volume=" + v.volume);
          v.muted = true;
          console.log("muted=" + v.muted);
          v.play();
          console.log("playing=" + (v.paused === false));
        });
        v.addEventListener("error", () => console.log("video-error"));
        a.addEventListener("error", () => console.log("audio-error"));
        v.addEventListener("timeupdate", () => {
          ticks = ticks + 1;
          if (ticks >= 2) {
            console.log("advanced=" + (v.currentTime > 2.4));
            win.close();
          }
        });
      });

      run();
      `,
      { autocloseMs: 8000, extensions: [mediaExtension] },
    );

    expect(stdout).not.toContain("video-error");
    expect(stdout).not.toContain("audio-error");
    expect(value("duration")).toBe("true:true");
    expect(value("dims")).toBe("true:true");
    expect(value("paused")).toBe("true");
    expect(value("seek")).toBe("true");
    expect(value("volume")).toBe("0.25");
    expect(value("muted")).toBe("true");
    expect(value("playing")).toBe("true");
    expect(value("advanced")).toBe("true");
  });

  it("runs the media example from the repository root", () => {
    const workdir = mkdtempSync(join(tmpdir(), "xbintsc-media-"));
    try {
      writeFileSync(join(workdir, "media.html"), readTextFile(join(exampleDir, "media.html")));
      writeFileSync(join(workdir, "media.ts"), readTextFile(join(exampleDir, "media.ts")));
      const extensions = createDefaultRegistry().register(guiExtension).register(mediaExtension);
      const result = build(join(workdir, "media.ts"), {
        emit: "exe",
        outDir: join(workdir, "out"),
        cacheDir: join(workdir, ".cache"),
        extensions,
      });
      expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);
      // The example's `src` paths are relative to the working directory.
      const executed = spawnSync(result.outputPath, [], {
        cwd: root,
        encoding: "utf8",
        env: { ...process.env, XT_GUI_AUTOCLOSE_MS: "8000" },
      });
      expect(executed.status, executed.stderr).toBe(0);
      expect(executed.stdout).toMatch(/video = \d+x\d+ duration=\d/);
      expect(executed.stdout).toMatch(/audio duration=\d/);
      expect(executed.stdout).toContain("play paused=false");
      expect(executed.stdout).toContain("closed");
    } finally {
      rmSync(workdir, { recursive: true, force: true });
    }
  });
});
