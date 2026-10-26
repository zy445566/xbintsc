/**
 * GUI e2e: DOM work a page module performs before `loadHTML`.
 *
 * Bundled page imports run before the host calls `loadHTML`, so a page that
 * touches the DOM from module scope exercises the pending-handle and detached-
 * node paths: `createElement`, `element.dataset`, and WebGL on a pending
 * `<canvas>`.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — pre-load DOM", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("builds nodes, reads dataset and sizes a canvas before loadHTML", () => {
    const { value } = harness.compileAndRun(
      "preload",
      `
      import { createWindow, run } from "gui";
      import { pendingCanvas, detachedCanvas } from "./probe.js";

      const win = createWindow({ title: "preload", width: 300, height: 200 });
      win.on("ready", () => {
        const scene = win.document.getElementById("scene");
        console.log("bound=" + (scene != null));
        console.log("size=" + pendingCanvas.width + "x" + pendingCanvas.height);
        console.log("gl=" + (scene.getContext("webgl2") != null));
        const data = win.document.getElementById("withdata");
        console.log("dataset=" + data.dataset.keyItem);
        data.dataset.chord = "F+D";
        console.log("chord=" + data.dataset.chord);
        console.log("detached=" + detachedCanvas.width + "x" + detachedCanvas.height + ":" + detachedCanvas.dataset.keyItem);
        win.close();
      });
      win.loadHTML("<div id='withdata' data-key-item='KeyF'></div><canvas id='scene'></canvas>");
      run();
      `,
      {
        files: {
          "probe.js": `
          const box = document.createElement("span");
          box.setAttribute("data-key-item", "KeyF");

          export const detachedCanvas = document.createElement("canvas");
          detachedCanvas.width = 64;
          detachedCanvas.height = 32;
          detachedCanvas.setAttribute("data-key-item", "Detached");
          console.log("created=" + (box.tagName === "SPAN") + ":" + box.dataset.keyItem);
          console.log("detached2d=" + (detachedCanvas.getContext("2d") != null));

          export const pendingCanvas = document.getElementById("scene");
          console.log("pending=" + (pendingCanvas != null));
          console.log("pendingGl=" + (pendingCanvas.getContext("webgl2") != null));
          pendingCanvas.width = 480;
          pendingCanvas.height = 320;
          `,
        },
      },
    );

    expect(value("created")).toBe("true:KeyF");
    expect(value("detached2d")).toBe("true");
    expect(value("pending")).toBe("true");
    expect(value("pendingGl")).toBe("true");
    expect(value("bound")).toBe("true");
    expect(value("size")).toBe("480x320");
    expect(value("gl")).toBe("true");
    expect(value("dataset")).toBe("KeyF");
    expect(value("chord")).toBe("F+D");
    expect(value("detached")).toBe("64x32:Detached");
  });
});
