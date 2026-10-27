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

  it("supports gradients, ellipses, rounded rects, dashes and line styles", () => {
    const { value } = harness.compileAndRun(
      "canvas-modern",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>body { margin: 0; }</style></head><body>
        <canvas id="c" width="240" height="260"></canvas>
      </body></html>
      \`;

      const win = createWindow({ title: "canvas-modern", width: 400, height: 300 });
      win.on("ready", () => {
        const canvas: any = win.document.getElementById("c");
        const ctx: any = canvas.getContext("2d");
        const px = (x: number, y: number): string => {
          const d = ctx.getImageData(x, y, 1, 1).data;
          return d[0] + "," + d[1] + "," + d[2] + "," + d[3];
        };

        ctx.fillStyle = "#ffffff";
        ctx.fillRect(0, 0, 240, 260);

        // A horizontal red -> blue gradient, sampled well inside both ends.
        const linear = ctx.createLinearGradient(0, 0, 100, 0);
        linear.addColorStop(0, "#ff0000");
        linear.addColorStop(1, "#0000ff");
        ctx.fillStyle = linear;
        ctx.fillRect(0, 0, 100, 20);
        console.log("grad-left=" + px(5, 10));
        console.log("grad-right=" + px(95, 10));
        // The getter returns the same handle, not a copy.
        console.log("same-gradient=" + (ctx.fillStyle === linear));

        // A stop added after assignment is live: the right side turns blue.
        const live = ctx.createLinearGradient(0, 0, 100, 0);
        live.addColorStop(0, "#ff0000");
        ctx.fillStyle = live;
        live.addColorStop(1, "#0000ff");
        ctx.fillRect(0, 30, 100, 20);
        console.log("live-right=" + px(95, 40));

        const radial = ctx.createRadialGradient(50, 70, 0, 50, 70, 35);
        radial.addColorStop(0, "#ffffff");
        radial.addColorStop(1, "#000000");
        ctx.fillStyle = radial;
        ctx.fillRect(0, 60, 100, 40);
        console.log("radial-centre=" + px(50, 70));
        console.log("radial-edge=" + px(88, 70));

        ctx.fillStyle = "#00ff00";
        ctx.beginPath();
        ctx.ellipse(170, 70, 40, 15, 0, 0, Math.PI * 2);
        ctx.fill();
        console.log("ellipse-centre=" + px(170, 70));
        console.log("ellipse-outside=" + px(170, 40));

        ctx.fillStyle = "#ff00ff";
        ctx.beginPath();
        ctx.roundRect(10, 100, 70, 50, 14);
        ctx.fill();
        console.log("roundrect-centre=" + px(45, 125));
        console.log("roundrect-corner=" + px(11, 101));

        ctx.strokeStyle = "#000000";
        ctx.lineWidth = 4;
        ctx.lineCap = "butt";
        ctx.setLineDash([10, 10]);
        ctx.lineDashOffset = 0;
        console.log("cap=" + ctx.lineCap);
        console.log("join=" + ctx.lineJoin);
        console.log("miter=" + ctx.miterLimit);
        console.log("dash=" + ctx.getLineDash().join(","));
        ctx.beginPath();
        ctx.moveTo(120, 150);
        ctx.lineTo(230, 150);
        ctx.stroke();
        console.log("dash-on=" + px(124, 150));
        console.log("dash-off=" + px(136, 150));

        ctx.setLineDash([]);
        ctx.strokeStyle = "#ff0000";
        ctx.lineWidth = 10;
        ctx.lineCap = "square";
        ctx.beginPath();
        ctx.moveTo(120, 190);
        ctx.lineTo(180, 190);
        ctx.stroke();
        console.log("square-cap=" + px(115, 190));
        console.log("square-cap-far=" + px(105, 190));

        // A miter fills the outer corner right to the tip; a bevel cuts it off.
        ctx.strokeStyle = "#000000";
        ctx.lineWidth = 20;
        ctx.lineCap = "butt";
        ctx.lineJoin = "miter";
        ctx.beginPath();
        ctx.moveTo(120, 215);
        ctx.lineTo(120, 245);
        ctx.lineTo(160, 245);
        ctx.stroke();
        console.log("miter-join=" + px(111, 254));
        ctx.lineJoin = "bevel";
        ctx.beginPath();
        ctx.moveTo(180, 215);
        ctx.lineTo(180, 245);
        ctx.lineTo(220, 245);
        ctx.stroke();
        console.log("bevel-join=" + px(171, 254));

        ctx.save();
        ctx.translate(100, 100);
        ctx.rotate(Math.PI / 2);
        const t = ctx.getTransform();
        console.log("nested-transform=" + Math.round(t.e) + "," + Math.round(t.f));
        // Post-multiplied: the square sits at (90,100)..(100,110), not at the
        // rotated origin. A pre-multiplied transform would put it elsewhere.
        ctx.fillStyle = "#ff8800";
        ctx.fillRect(0, 0, 10, 10);
        console.log("rotated-rect=" + px(95, 105));
        ctx.restore();
        const r = ctx.getTransform();
        console.log("restored-transform=" + r.a + "," + r.e + "," + r.f);

        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    const channels = (label: string): number[] => value(label).split(",").map(Number);

    const left = channels("grad-left");
    expect(left[0]).toBeGreaterThan(200);
    expect(left[2]).toBeLessThan(60);
    const right = channels("grad-right");
    expect(right[0]).toBeLessThan(60);
    expect(right[2]).toBeGreaterThan(200);
    expect(value("same-gradient")).toBe("true");
    expect(channels("live-right")[2]).toBeGreaterThan(200);

    // The radial gradient is white at its centre and black at its edge.
    expect(channels("radial-centre")[0]).toBeGreaterThan(230);
    expect(channels("radial-edge")[0]).toBeLessThan(40);

    expect(channels("ellipse-centre")).toEqual([0, 255, 0, 255]);
    expect(channels("ellipse-outside")).toEqual([255, 255, 255, 255]);
    expect(channels("roundrect-centre")).toEqual([255, 0, 255, 255]);
    expect(channels("roundrect-corner")).toEqual([255, 255, 255, 255]);

    expect(value("cap")).toBe("butt");
    expect(value("join")).toBe("miter");
    expect(value("miter")).toBe("10");
    expect(value("dash")).toBe("10,10");
    expect(channels("dash-on")).toEqual([0, 0, 0, 255]);
    expect(channels("dash-off")).toEqual([255, 255, 255, 255]);
    // A square cap extends half a line width past the endpoint.
    expect(channels("square-cap")).toEqual([255, 0, 0, 255]);
    expect(channels("square-cap-far")).toEqual([255, 255, 255, 255]);
    expect(channels("miter-join")).toEqual([0, 0, 0, 255]);
    expect(channels("bevel-join")).toEqual([255, 255, 255, 255]);

    expect(value("nested-transform")).toBe("100,100");
    expect(channels("rotated-rect")).toEqual([255, 136, 0, 255]);
    expect(value("restored-transform")).toBe("1,0,0");
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
