/**
 * `<video>` / `<audio>` are part of the GUI DOM even when no decoder is linked.
 *
 * This suite compiles with `--ext gui` only (the harness never registers the
 * `media` extension), which is the whole point of the split: a GUI program does
 * not link FFmpeg unless it asks for it. The element still exists and its DOM
 * API is callable, but opening a source fails and fires `error`.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

describe.skipIf(!guiAvailable)("gui extension without media", () => {
  const harness = createGuiHarness();
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("reports error instead of decoding when the media extension is off", () => {
    const { stdout } = harness.compileAndRun(
      "media-off",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "media off", width: 320, height: 200 });
      win.on("ready", () => {
        win.loadHTML('<video id="v" src="missing.mp4"></video>');
        const v: any = win.document.getElementById("v");
        v.addEventListener("loadedmetadata", () => {
          console.log("unexpected-metadata");
          win.close();
        });
        v.addEventListener("error", () => {
          console.log("media-error");
          win.close();
        });
        // The DOM surface is still there even without a decoder.
        console.log("api=" + (v.paused === true) + ":" + (v.duration === 0));
      });

      run();
      `,
      { autocloseMs: 1500 },
    );

    expect(stdout).toContain("media-error");
    expect(stdout).not.toContain("unexpected-metadata");
    expect(stdout).toContain("api=true:true");
  });
});
