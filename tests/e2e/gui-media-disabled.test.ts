/**
 * `<video>` / `<audio>` are part of the GUI DOM even when no decoder is linked.
 *
 * This suite compiles with `--ext gui` and explicitly disables the `media` link
 * feature (`noFeatures: ["media"]`), which is the escape hatch for a program
 * that wants the DOM surface but not FFmpeg (or runs on a host without the
 * archive). The element still exists and its DOM API is callable, but opening a
 * source fails and fires `error`.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

describe.skipIf(!guiAvailable)("gui extension without media", () => {
  const harness = createGuiHarness();
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("reports error instead of decoding when the media feature is off", () => {
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
      { autocloseMs: 1500, noFeatures: ["media"] },
    );

    expect(stdout).toContain("media-error");
    expect(stdout).not.toContain("unexpected-metadata");
    expect(stdout).toContain("api=true:true");
  });
});
