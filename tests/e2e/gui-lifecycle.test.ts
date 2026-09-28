/**
 * End-to-end tests for the `gui` extension — window lifecycle and the frame
 * loop (`requestAnimationFrame` / `cancelAnimationFrame`).
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — lifecycle", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("opens a GPU-backed window and delivers lifecycle events", () => {
    const { stdout } = harness.compileAndRun(
      "window",
      `
      import { createWindow, run } from "gui";
      const win = createWindow({ title: "test window", width: 320, height: 240 });
      win.on("ready", () => console.log("gui ready"));
      win.on("close", () => console.log("gui closed"));
      win.setBackground("#101010");
      win.loadHTML("<h1>hi</h1>");
      run();
      console.log("gui exited");
      `,
    );
    const lines = stdout.trim().split("\n");
    expect(lines).toContain("gui ready");
    expect(lines).toContain("gui closed");
    expect(lines).toContain("gui exited");
  });

  it("runs requestAnimationFrame callbacks each frame and honours cancelAnimationFrame", () => {
    const { stdout, value } = harness.compileAndRun(
      "raf",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "raf", width: 320, height: 240 });
      let frames = 0;
      const tick = (t: number) => {
        frames = frames + 1;
        win.document.getElementById("label").textContent = String(frames);
        if (frames < 3) win.requestAnimationFrame(tick);
        else {
          console.log("frames=" + frames + ":" + (t > 0));
          console.log("label=" + win.document.getElementById("label").textContent);
          win.close();
        }
      };
      win.on("ready", () => {
        const cancelled = win.requestAnimationFrame(() => console.log("cancelled-ran"));
        win.cancelAnimationFrame(cancelled);
        const id = win.requestAnimationFrame(tick);
        console.log("id=" + (id > 0));
      });
      win.loadHTML("<div id='label'>0</div>");
      run();
      `,
    );
    expect(value("id")).toBe("true");
    expect(value("frames")).toBe("3:true"); // callback receives a timestamp
    expect(value("label")).toBe("3"); // the DOM write was visible on the next read
    expect(stdout).not.toContain("cancelled-ran");
  });
});
