/**
 * End-to-end tests for the argument-honouring and image halves of the canvas
 * 2D API: fill rules, Path2D, maxWidth, radius lists, matrix transforms,
 * context attributes, putImageData, createPattern and PNG export.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — canvas arguments and images", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());
  it("honours fill rules, Path2D, maxWidth, radii, matrices and context attributes", () => {
    const { value } = harness.compileAndRun(
      "canvas-arguments",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>body { margin: 0; }</style></head><body>
        <canvas id="c" width="200" height="200"></canvas>
        <canvas id="c2" width="20" height="20"></canvas>
      </body></html>
      \`;

      const win = createWindow({ title: "canvas-arguments", width: 400, height: 300 });
      win.on("ready", () => {
        const canvas: any = win.document.getElementById("c");
        const ctx: any = canvas.getContext("2d");
        const px = (x: number, y: number): string => {
          const d = ctx.getImageData(x, y, 1, 1).data;
          return d[0] + "," + d[1] + "," + d[2] + "," + d[3];
        };
        const white = (): void => {
          ctx.fillStyle = "#ffffff";
          ctx.fillRect(0, 0, 200, 200);
        };

        white();
        ctx.fillStyle = "#ff0000";
        ctx.beginPath();
        ctx.rect(10, 10, 80, 80);
        ctx.rect(30, 30, 40, 40);
        ctx.fill();
        console.log("nonzero-hole=" + px(50, 50));
        white();
        ctx.fillStyle = "#ff0000";
        ctx.beginPath();
        ctx.rect(10, 10, 80, 80);
        ctx.rect(30, 30, 40, 40);
        ctx.fill("evenodd");
        console.log("evenodd-hole=" + px(50, 50));

        // Path2D: fill and stroke a path built separately from the context.
        white();
        const path: any = new Path2D();
        path.rect(110, 10, 40, 40);
        ctx.fillStyle = "#00ff00";
        ctx.fill(path);
        console.log("path-fill=" + px(130, 30));
        path.rect(110, 70, 40, 40);
        ctx.strokeStyle = "#0000ff";
        ctx.lineWidth = 4;
        ctx.stroke(path);
        console.log("path-stroke=" + px(130, 70));

        // roundRect radii list: square top-right corner, round top-left.
        white();
        ctx.fillStyle = "#ff00ff";
        ctx.beginPath();
        ctx.roundRect(10, 120, 60, 60, [20, 0, 0, 20]);
        ctx.fill();
        console.log("radius-tl=" + px(12, 122));
        console.log("radius-tr=" + px(68, 122));

        // setTransform/transform accept a matrix object.
        white();
        ctx.setTransform({ a: 2, b: 0, c: 0, d: 2, e: 5, f: 5 });
        ctx.fillStyle = "#000000";
        ctx.fillRect(0, 0, 10, 10);
        console.log("matrix-set=" + px(6, 6));
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.transform({ a: 1, b: 0, c: 0, d: 1, e: 30, f: 30 });
        ctx.fillStyle = "#123456";
        ctx.fillRect(0, 0, 5, 5);
        console.log("matrix-transform=" + px(32, 32));
        ctx.setTransform(1, 0, 0, 1, 0, 0);

        // fillText maxWidth condenses the run instead of ignoring the argument.
        const extent = (): string => {
          const data = ctx.getImageData(0, 80, 200, 40).data;
          let min = 9999;
          let max = -1;
          for (let y = 0; y < 40; y++) {
            for (let x = 0; x < 200; x++) {
              const i = (y * 200 + x) * 4;
              if (data[i] < 250) {
                if (x < min) min = x;
                if (x > max) max = x;
              }
            }
          }
          return min + ":" + max;
        };
        white();
        ctx.fillStyle = "#000000";
        ctx.font = "20px sans-serif";
        const full = ctx.measureText("Hello").width;
        ctx.fillText("Hello", 10, 100);
        const fullExtent = extent();
        white();
        ctx.fillStyle = "#000000";
        ctx.fillText("Hello", 10, 100, full / 2);
        const squeezedExtent = extent();
        const fullWidth = Number(fullExtent.split(":")[1]) - Number(fullExtent.split(":")[0]);
        const squeezedWidth =
          Number(squeezedExtent.split(":")[1]) - Number(squeezedExtent.split(":")[0]);
        console.log("text=" + fullWidth + ":" + squeezedWidth + ":" + (squeezedWidth < fullWidth) +
                    ":" + (squeezedWidth <= full / 2 + 2));

        // getContextAttributes and the alpha effect.
        const opaque: any = win.document.getElementById("c2");
        const opaqueCtx: any = opaque.getContext("2d", { alpha: false });
        const attrs: any = opaqueCtx.getContextAttributes();
        console.log("attrs=" + attrs.alpha + ":" + attrs.antialias + ":" + attrs.colorSpace);
        const corner = opaqueCtx.getImageData(0, 0, 1, 1).data;
        console.log("opaque=" + corner[0] + "," + corner[1] + "," + corner[2] + "," + corner[3]);
        const transparent: any = win.document.getElementById("c");
        console.log("same-ctx=" + (transparent.getContext("2d", { alpha: false }) === ctx));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("nonzero-hole")).toBe("255,0,0,255");
    expect(value("evenodd-hole")).toBe("255,255,255,255");
    expect(value("path-fill")).toBe("0,255,0,255");
    expect(value("path-stroke")).toBe("0,0,255,255");
    expect(value("radius-tl")).toBe("255,255,255,255");
    expect(value("radius-tr")).toBe("255,0,255,255");
    expect(value("matrix-set")).toBe("0,0,0,255");
    expect(value("matrix-transform")).toBe("18,52,86,255");
    expect(value("text")).toMatch(/^\d+:\d+:true:true$/);
    expect(value("attrs")).toBe("false:true:srgb");
    expect(value("opaque")).toBe("0,0,0,255");
    expect(value("same-ctx")).toBe("true");
  });
  it("implements putImageData, createPattern and PNG export", () => {
    const { value, stdout } = harness.compileAndRun(
      "canvas-image-apis",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>body { margin: 0; }</style></head><body>
        <canvas id="c" width="100" height="100"></canvas>
        <canvas id="tile" width="2" height="2"></canvas>
      </body></html>
      \`;

      const win = createWindow({ title: "canvas-image-apis", width: 400, height: 300 });
      win.on("ready", () => {
        const doc = win.document;
        const canvas: any = doc.getElementById("c");
        const ctx: any = canvas.getContext("2d");
        const px = (x: number, y: number): string => {
          const d = ctx.getImageData(x, y, 1, 1).data;
          return d[0] + "," + d[1] + "," + d[2] + "," + d[3];
        };

        // putImageData writes raw pixels and ignores the transform.
        const image: any = ctx.createImageData(2, 2);
        console.log("idata=" + image.width + "x" + image.height + ":" + image.data.length);
        for (let i = 0; i < image.data.length; i += 4) {
          image.data[i] = 255;
          image.data[i + 3] = 255;
        }
        ctx.setTransform(2, 0, 0, 2, 0, 0);
        ctx.putImageData(image, 10, 10);
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        console.log("put=" + px(10, 10) + ":" + px(11, 11) + ":" + px(9, 9) + ":" + px(20, 20));

        // createPattern tiles a source canvas in user space.
        const tile: any = doc.getElementById("tile");
        const tileCtx: any = tile.getContext("2d");
        tileCtx.fillStyle = "#ff0000";
        tileCtx.fillRect(0, 0, 1, 2);
        tileCtx.fillStyle = "#0000ff";
        tileCtx.fillRect(1, 0, 1, 2);
        const pattern: any = ctx.createPattern(tile, "repeat");
        console.log("pattern=" + (pattern != null));
        ctx.fillStyle = pattern;
        ctx.fillRect(40, 40, 10, 10);
        console.log("pat=" + px(40, 40) + ":" + px(41, 40) + ":" + px(40, 41) + ":" + px(42, 40));
        console.log("same-pattern=" + (ctx.fillStyle === pattern));
        // A colour assignment replaces the pattern again.
        ctx.fillStyle = "#00ff00";
        console.log("style=" + ctx.fillStyle);

        // PNG export: data URL prefix + Blob-shaped callback result.
        const url: string = canvas.toDataURL("image/png");
        console.log("url=" + url.slice(0, 22) + ":" + (url.length > 100));
        canvas.toBlob((blob: any) => {
          console.log("blob=" + blob.type + ":" + (blob.size > 100) + ":" +
                      (blob.__xt_gui_blob_data.length === blob.size));
          win.close();
        }, "image/png");
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("idata")).toBe("2x2:16");
    expect(value("put")).toBe("255,0,0,255:255,0,0,255:0,0,0,0:0,0,0,0");
    expect(value("pattern")).toBe("true");
    expect(value("pat")).toBe("255,0,0,255:0,0,255,255:255,0,0,255:255,0,0,255");
    expect(value("same-pattern")).toBe("true");
    expect(value("style")).toBe("#00ff00");
    expect(value("url")).toBe("data:image/png;base64,:true");
    expect(stdout).toContain("blob=image/png:true:true");
  });
});
