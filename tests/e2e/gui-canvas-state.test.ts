/**
 * End-to-end tests for the Canvas2D features added after the first canvas
 * slice: clipping, compositing, shadows, hit testing, smoothing, data-URL
 * backgrounds and `OffscreenCanvas`.
 *
 * They run the real rasteriser and read pixels back with `getImageData`, so a
 * regression in coverage/alpha math shows up as a concrete colour.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><style>body { margin: 0; }</style></head><body>
  <canvas id="c" width="120" height="120"></canvas>
  <canvas id="src" width="20" height="20"></canvas>
  <canvas id="transferred" width="20" height="20"></canvas>
  <div id="bg" style="width:40px;height:40px"></div>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — canvas state, clip and offscreen", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("clips drawing and composites with the Porter-Duff operators", () => {
    const { value } = harness.compileAndRun(
      "canvas-clip-composite",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "clip", width: 400, height: 300 });
      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const ctx: any = canvas.getContext("2d");
        const px = (x: number, y: number): string => {
          const d = ctx.getImageData(x, y, 1, 1).data;
          return d[0] + "," + d[1] + "," + d[2] + "," + d[3];
        };

        ctx.fillStyle = "#ffffff";
        ctx.fillRect(0, 0, 120, 120);

        // A rectangular clip keeps a red fill inside (0,0)-(50,50).
        ctx.save();
        ctx.beginPath();
        ctx.rect(0, 0, 50, 50);
        ctx.clip();
        ctx.fillStyle = "#ff0000";
        ctx.fillRect(0, 0, 120, 120);
        ctx.restore();
        console.log("clip-in=" + px(25, 25));
        console.log("clip-out=" + px(75, 25));
        // restore() brings the unclipped region back.
        ctx.fillStyle = "#00ff00";
        ctx.fillRect(75, 75, 20, 20);
        console.log("after-restore=" + px(80, 80));

        // A Path2D clip.
        const path: any = new Path2D();
        path.rect(60, 0, 20, 20);
        ctx.save();
        ctx.clip(path);
        ctx.fillStyle = "#0000ff";
        ctx.fillRect(60, 0, 20, 20);
        ctx.restore();
        console.log("path-clip=" + px(65, 5));

        // destination-out clears the covered pixels to transparent.
        ctx.globalCompositeOperation = "destination-out";
        ctx.fillStyle = "#000000";
        ctx.fillRect(0, 0, 20, 20);
        console.log("composite=" + ctx.globalCompositeOperation);
        console.log("dest-out=" + px(10, 10));
        ctx.globalCompositeOperation = "source-over";

        // isPointInPath / isPointInStroke use the current path and transform.
        ctx.beginPath();
        ctx.rect(10, 10, 20, 20);
        console.log("point-in=" + ctx.isPointInPath(15, 15));
        console.log("point-out=" + ctx.isPointInPath(50, 50));
        ctx.lineWidth = 6;
        ctx.beginPath();
        ctx.moveTo(80, 80);
        ctx.lineTo(110, 80);
        console.log("stroke-in=" + ctx.isPointInStroke(95, 81));
        console.log("stroke-out=" + ctx.isPointInStroke(95, 95));
        win.close();
      });
      win.loadHTML(\`${PAGE}\`);
      run();
      `,
    );
    expect(value("clip-in")).toBe("255,0,0,255");
    expect(value("clip-out")).toBe("255,255,255,255");
    expect(value("after-restore")).toBe("0,255,0,255");
    expect(value("path-clip")).toBe("0,0,255,255");
    expect(value("composite")).toBe("destination-out");
    // destination-out on an opaque white pixel leaves transparent black.
    expect(value("dest-out")).toBe("0,0,0,0");
    expect(value("point-in")).toBe("true");
    expect(value("point-out")).toBe("false");
    expect(value("stroke-in")).toBe("true");
    expect(value("stroke-out")).toBe("false");
  });

  it("paints offset shadows under the shape", () => {
    const { value } = harness.compileAndRun(
      "canvas-shadow",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "shadow", width: 400, height: 300 });
      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const ctx: any = canvas.getContext("2d");
        const px = (x: number, y: number): string => {
          const d = ctx.getImageData(x, y, 1, 1).data;
          return d[0] + "," + d[1] + "," + d[2] + "," + d[3];
        };

        ctx.shadowColor = "#000000";
        ctx.shadowBlur = 0;
        ctx.shadowOffsetX = 10;
        ctx.shadowOffsetY = 10;
        ctx.fillStyle = "#ff0000";
        ctx.fillRect(10, 10, 20, 20);
        console.log("color=" + ctx.shadowColor);
        console.log("offsets=" + ctx.shadowOffsetX + "," + ctx.shadowOffsetY);
        console.log("shape=" + px(20, 20));
        console.log("shadow=" + px(35, 35));
        console.log("outside=" + px(5, 5));

        // The shadow is not part of clearRect, and a transparent shadow
        // colour disables it again.
        ctx.shadowColor = "rgba(0,0,0,0)";
        ctx.fillStyle = "#0000ff";
        ctx.fillRect(60, 10, 10, 10);
        console.log("no-shadow=" + px(75, 15));
        win.close();
      });
      win.loadHTML(\`${PAGE}\`);
      run();
      `,
    );
    expect(value("color")).toBe("#000000");
    expect(value("offsets")).toBe("10,10");
    expect(value("shape")).toBe("255,0,0,255");
    expect(value("shadow")).toBe("0,0,0,255");
    expect(value("outside")).toBe("0,0,0,0");
    expect(value("no-shadow")).toBe("0,0,0,0");
  });

  it("honours image smoothing and the inner radial circle", () => {
    const { value } = harness.compileAndRun(
      "canvas-smoothing-radial",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "smoothing", width: 400, height: 300 });
      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const ctx: any = canvas.getContext("2d");
        const px = (x: number, y: number): string => {
          const d = ctx.getImageData(x, y, 1, 1).data;
          return d[0] + "," + d[1] + "," + d[2] + "," + d[3];
        };

        // A 2x2 source (red left, blue right), blown up to 40x40.
        const source: any = new OffscreenCanvas(2, 2);
        const sctx: any = source.getContext("2d");
        sctx.putImageData({ width: 2, height: 2, data: [
          255, 0, 0, 255, 0, 0, 255, 255,
          255, 0, 0, 255, 0, 0, 255, 255] }, 0, 0);

        console.log("smoothing-default=" + ctx.imageSmoothingEnabled);
        ctx.drawImage(source, 0, 0, 40, 40);
        console.log("smooth=" + px(20, 20));
        ctx.clearRect(0, 0, 120, 120);
        ctx.imageSmoothingEnabled = false;
        console.log("smoothing-off=" + ctx.imageSmoothingEnabled);
        ctx.drawImage(source, 0, 0, 40, 40);
        console.log("blocky=" + px(20, 20));
        ctx.imageSmoothingQuality = "high";
        console.log("quality=" + ctx.imageSmoothingQuality);

        // A radial gradient whose inner circle is off-centre: the point at the
        // inner centre is white, the outer circle black. (A concentric
        // implementation would make both black.)
        const radial = ctx.createRadialGradient(20, 50, 0, 50, 50, 30);
        radial.addColorStop(0, "#ffffff");
        radial.addColorStop(1, "#000000");
        ctx.fillStyle = radial;
        ctx.fillRect(0, 30, 100, 40);
        console.log("radial-inner=" + px(20, 50));
        console.log("radial-outer=" + px(80, 50));
        win.close();
      });
      win.loadHTML(\`${PAGE}\`);
      run();
      `,
    );
    expect(value("smoothing-default")).toBe("true");
    // Bilinear samples between the red and blue texels.
    const smooth = value("smooth").split(",").map(Number);
    expect(smooth[0]).toBeGreaterThan(20);
    expect(smooth[2]).toBeGreaterThan(20);
    expect(value("smoothing-off")).toBe("false");
    expect(value("blocky")).toBe("0,0,255,255");
    expect(value("quality")).toBe("high");
    const radialInner = value("radial-inner").split(",").map(Number);
    expect(radialInner[0]).toBeGreaterThan(245);
    expect(value("radial-outer")).toBe("0,0,0,255");
  });

  it("supports OffscreenCanvas and transferControlToOffscreen", () => {
    const { value } = harness.compileAndRun(
      "canvas-offscreen",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "offscreen", width: 400, height: 300 });
      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const ctx: any = canvas.getContext("2d");
        const px = (x: number, y: number): string => {
          const d = ctx.getImageData(x, y, 1, 1).data;
          return d[0] + "," + d[1] + "," + d[2] + "," + d[3];
        };

        const off: any = new OffscreenCanvas(20, 10);
        console.log("size=" + off.width + "x" + off.height);
        console.log("instanceof=" + (off instanceof OffscreenCanvas));
        const octx: any = off.getContext("2d");
        console.log("ctx=" + (octx != null) + "," + (off.getContext("2d") === octx) + "," +
          (octx.canvas === off));
        octx.fillStyle = "#00ff00";
        octx.fillRect(0, 0, 20, 10);
        ctx.drawImage(off, 0, 0);
        console.log("drawn=" + px(10, 5));

        const bitmap: any = off.transferToImageBitmap();
        console.log("bitmap=" + bitmap.width + "x" + bitmap.height);

        // alpha:false starts as opaque black, like a DOM canvas.
        const opaque: any = new OffscreenCanvas(4, 4);
        const opaqueCtx: any = opaque.getContext("2d", { alpha: false });
        const opaquePixel = opaqueCtx.getImageData(0, 0, 1, 1).data;
        console.log("opaque=" + opaqueCtx.getContextAttributes().alpha + "," +
          opaquePixel[0] + "," + opaquePixel[1] + "," + opaquePixel[2] + "," + opaquePixel[3]);

        // Transfer a live element: the element loses its context, the returned
        // OffscreenCanvas keeps drawing into the same bitmap.
        const element: any = document.getElementById("transferred");
        const transferred: any = element.transferControlToOffscreen();
        console.log("transferred=" + (transferred != null));
        console.log("element-ctx=" + (element.getContext("2d") == null));
        const tctx: any = transferred.getContext("2d");
        tctx.fillStyle = "#ff00ff";
        tctx.fillRect(0, 0, 20, 20);
        ctx.drawImage(transferred, 50, 50);
        console.log("transfer-draw=" + px(60, 60));

        // convertToBlob resolves to a PNG blob.
        off.convertToBlob().then((blob: any): void => {
          console.log("blob=" + blob.type + "," + (blob.size > 0));
          win.close();
        });
      });
      win.loadHTML(\`${PAGE}\`);
      run();
      `,
      { autocloseMs: 2000 },
    );
    expect(value("size")).toBe("20x10");
    expect(value("instanceof")).toBe("true");
    expect(value("ctx")).toBe("true,true,true");
    expect(value("drawn")).toBe("0,255,0,255");
    expect(value("bitmap")).toBe("20x10");
    expect(value("opaque")).toBe("false,0,0,0,255");
    expect(value("transferred")).toBe("true");
    expect(value("element-ctx")).toBe("true");
    expect(value("transfer-draw")).toBe("255,0,255,255");
    expect(value("blob")).toBe("image/png,true");
  });

  it("paints a canvas data URL as a CSS background-image", () => {
    const { value } = harness.compileAndRun(
      "canvas-background-image",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "bg", width: 400, height: 300 });
      win.on("ready", () => {
        const source: any = document.getElementById("src");
        const sctx: any = source.getContext("2d");
        sctx.fillStyle = "#ff00ff";
        sctx.fillRect(0, 0, 20, 20);
        const url: string = source.toDataURL();
        const bg: any = document.getElementById("bg");
        bg.style.backgroundImage = "url(" + url + ")";
        const list: string = win.paintList();
        console.log("has-image=" + (list.indexOf("src=data:image/png;base64,") >= 0));
        const computed: string = getComputedStyle(bg).backgroundImage;
        console.log("inline=" + (bg.style.backgroundImage.indexOf("url(") === 0));
        console.log("computed-width=" + getComputedStyle(bg).width);
        console.log("computed-head=" + computed.substring(0, 24));
        console.log("computed=" + (computed.indexOf("url(data:image/png") === 0));
        win.close();
      });
      win.loadHTML(\`${PAGE}\`);
      run();
      `,
    );
    expect(value("has-image")).toBe("true");
    expect(value("inline")).toBe("true");
    expect(value("computed-width")).toBe("40px");
    expect(value("computed")).toBe("true");
  });
});
