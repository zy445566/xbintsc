/**
 * End-to-end tests for `<canvas>` support in the `gui` extension.
 *
 * A `<canvas>` is a replaced element laid out like an `<img>`, but its pixels
 * come from a CPU rasteriser instead of a decoder. The script draws through the
 * 2D context and the engine turns the bitmap into a GPU texture through the same
 * image path as `media_id`/`src` (see `renderer_image.cpp`).
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — canvas", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("lays out a canvas and rasterises fills into pixels", () => {
    const { value, stdout } = harness.compileAndRun(
      "canvas-draw",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #c { width: 100px; height: 50px; }
      </style></head><body>
        <canvas id="c" width="100" height="50"></canvas>
      </body></html>
      \`;

      const win = createWindow({ title: "canvas", width: 400, height: 300 });
      win.on("ready", () => {
        const canvas: any = win.document.getElementById("c");
        const ctx: any = canvas.getContext("2d");
        ctx.fillStyle = "#ff0000";
        ctx.fillRect(0, 0, 50, 50);
        ctx.fillStyle = "#0000ff";
        ctx.fillRect(50, 0, 50, 50);

        const left = ctx.getImageData(0, 0, 1, 1).data;
        const right = ctx.getImageData(99, 0, 1, 1).data;
        console.log("left=" + left[0] + "," + left[1] + "," + left[2] + "," + left[3]);
        console.log("right=" + right[0] + "," + right[1] + "," + right[2] + "," + right[3]);

        console.log("size=" + canvas.width + "x" + canvas.height);
        const rect = win.getBoundingClientRect("#c");
        console.log("rect=" + rect.x + "," + rect.y + "," + rect.width + "," + rect.height);
        console.log("paint-list-start");
        console.log(win.paintList());
        console.log("paint-list-end");

        // The same context handle is returned on every getContext("2d").
        console.log("same-ctx=" + (canvas.getContext("2d") === ctx));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    const list = stdout.slice(stdout.indexOf("paint-list-start"), stdout.indexOf("paint-list-end"));

    // The canvas keeps its attribute size and is laid out as a replaced block.
    expect(value("size")).toBe("100x50");
    expect(value("rect")).toBe("0,0,100,50");
    // The rasteriser filled the left half red and the right half blue.
    expect(value("left")).toBe("255,0,0,255");
    expect(value("right")).toBe("0,0,255,255");
    // The canvas reaches the display list as an image keyed by its canvas id.
    expect(list).toMatch(/image x=0\.0 y=0\.0 w=100\.0 h=50\.0 canvas=\d+/);
    expect(value("same-ctx")).toBe("true");
  });

  it("honours attributes, the default size and the width/height setters", () => {
    const { value } = harness.compileAndRun(
      "canvas-size",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
      </style></head><body>
        <canvas id="a" width="80" height="40"></canvas>
        <canvas id="b"></canvas>
      </body></html>
      \`;

      const win = createWindow({ title: "canvas-size", width: 400, height: 300 });
      win.on("ready", () => {
        const a: any = win.document.getElementById("a");
        const b: any = win.document.getElementById("b");
        console.log("a=" + a.width + "x" + a.height);
        console.log("b=" + b.width + "x" + b.height);

        // Without CSS the canvas lays out at its attribute size.
        const rectA = win.getBoundingClientRect("#a");
        console.log("rect-a=" + rectA.width + "x" + rectA.height);

        b.width = 64;
        b.height = 32;
        console.log("b2=" + b.width + "x" + b.height);
        const rectB = win.getBoundingClientRect("#b");
        console.log("rect-b=" + rectB.width + "x" + rectB.height);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("a")).toBe("80x40");
    // The CSS default canvas size is 300x150.
    expect(value("b")).toBe("300x150");
    expect(value("rect-a")).toBe("80x40");
    expect(value("b2")).toBe("64x32");
    expect(value("rect-b")).toBe("64x32");
  });

  it("applies transforms, paths and text metrics", () => {
    const { value } = harness.compileAndRun(
      "canvas-paths",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>body { margin: 0; }</style></head><body>
        <canvas id="c" width="100" height="100"></canvas>
      </body></html>
      \`;

      const win = createWindow({ title: "canvas-paths", width: 400, height: 300 });
      win.on("ready", () => {
        const canvas: any = win.document.getElementById("c");
        const ctx: any = canvas.getContext("2d");

        // Translate 10px right, then fill a 5x5 square: pixel (10,0) is painted.
        ctx.save();
        ctx.translate(10, 0);
        ctx.fillStyle = "#00ff00";
        ctx.fillRect(0, 0, 5, 5);
        ctx.restore();
        const moved = ctx.getImageData(10, 0, 1, 1).data;
        const origin = ctx.getImageData(0, 0, 1, 1).data;
        console.log("moved=" + moved[0] + "," + moved[1] + "," + moved[2] + "," + moved[3]);
        console.log("origin=" + origin[3]);

        // A path fill through beginPath + rect.
        ctx.beginPath();
        ctx.fillStyle = "#ff00ff";
        ctx.rect(60, 60, 20, 20);
        ctx.fill();
        const path = ctx.getImageData(70, 70, 1, 1).data;
        console.log("path=" + path[0] + "," + path[1] + "," + path[2] + "," + path[3]);

        // save/restore also restores the paint style, not just the transform.
        ctx.fillStyle = "#ff0000";
        ctx.save();
        ctx.fillStyle = "#0000ff";
        ctx.restore();
        console.log("fill=" + ctx.fillStyle);

        console.log("text=" + (ctx.measureText("Hello").width > 0));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("moved")).toBe("0,255,0,255");
    // The transform is undone by `restore`, so the origin stays transparent.
    expect(value("origin")).toBe("0");
    expect(value("path")).toBe("255,0,255,255");
    expect(value("fill")).toBe("#ff0000");
    expect(value("text")).toBe("true");
  });

  it("draws an image onto a canvas with drawImage", () => {
    const src = harness.imageSrc(harness.logoPath);
    const { value } = harness.compileAndRun(
      "canvas-drawimage",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>body { margin: 0; }</style></head><body>
        <img id="logo" src="${src}" style="display:none">
        <canvas id="c" width="40" height="40"></canvas>
      </body></html>
      \`;

      const win = createWindow({ title: "canvas-drawimage", width: 400, height: 300 });
      win.on("ready", () => {
        const canvas: any = win.document.getElementById("c");
        const ctx: any = canvas.getContext("2d");
        const logo: any = win.document.getElementById("logo");
        // Blow the 4x4 logo up to 40x40.
        ctx.drawImage(logo, 0, 0, 40, 40);
        // The 4x4 logo is red on the left half, blue on the right half.
        const left = ctx.getImageData(4, 20, 1, 1).data;
        const right = ctx.getImageData(36, 20, 1, 1).data;
        console.log("left=" + left[0] + "," + left[1] + "," + left[2] + "," + left[3]);
        console.log("right=" + right[0] + "," + right[1] + "," + right[2] + "," + right[3]);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("left")).toBe("255,0,0,255");
    expect(value("right")).toBe("0,0,255,255");
  });

  it("releases canvases when the document is replaced", () => {
    const { value } = harness.compileAndRun(
      "canvas-lifecycle",
      `
      import { createWindow, run } from "gui";

      const FIRST = \`
      <html><body style="margin:0">
        <canvas id="c" width="20" height="20"></canvas>
      </body></html>
      \`;
      const SECOND = \`
      <html><body style="margin:0">
        <canvas id="c" width="30" height="30"></canvas>
      </body></html>
      \`;

      const win = createWindow({ title: "canvas-lifecycle", width: 400, height: 300 });
      win.on("ready", () => {
        const oldCtx: any = win.document.getElementById("c").getContext("2d");
        oldCtx.fillStyle = "#ff0000";
        oldCtx.fillRect(0, 0, 20, 20);

        // Replacing the document drops the old canvas (and its GPU texture).
        win.loadHTML(SECOND);
        const freshCtx: any = win.document.getElementById("c").getContext("2d");
        freshCtx.fillStyle = "#00ff00";
        freshCtx.fillRect(0, 0, 30, 30);
        const px = freshCtx.getImageData(0, 0, 1, 1).data;
        console.log("fresh=" + px[0] + "," + px[1] + "," + px[2] + "," + px[3]);
        // The stale context is inert rather than crashing.
        console.log("stale=" + (oldCtx.getImageData(0, 0, 1, 1).width === undefined));
        win.close();
      });
      win.loadHTML(FIRST);
      run();
      `,
    );
    expect(value("fresh")).toBe("0,255,0,255");
    expect(value("stale")).toBe("true");
  });
});
