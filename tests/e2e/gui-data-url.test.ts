/**
 * End-to-end tests for `data:` image URLs: raster and SVG, base64 and
 * percent-encoded payloads, in `<img>`, `canvas.drawImage(String, …)` and CSS
 * `background-image`.
 *
 * The SVG below deliberately exercises the decoder, not NanoSVG's feature set:
 * the engine rasterises NanoSVG's subset, so an SVG that uses filters still
 * decodes but renders the shapes without the filter effect.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

/* 4x4 PNG: left half red, right half blue. */
const PNG =
  "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+AAAAFElEQVR42mP4z8DwH4Sh1H8G0gUALFAf4eNWqTEAAAAASUVORK5CYII=";
const SVG =
  '<svg xmlns="http://www.w3.org/2000/svg" width="8" height="6"><rect width="8" height="6" fill="#ff0000"/></svg>';
const SVG_B64 = Buffer.from(SVG).toString("base64");
const SVG_ENC = encodeURIComponent(SVG);

/** The `feTurbulence` example from the request: `<filter>` is not in NanoSVG's
 * subset, but the document still decodes (the rect renders, unfiltered). */
const NOISE =
  "data:image/svg+xml,%3Csvg viewBox='0 0 180 180' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.82' numOctaves='4' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.8'/%3E%3C/svg%3E";

const PAGE = `
<html><head><style>body { margin: 0; }</style></head><body>
<img id="vec" src="data:image/svg+xml,${SVG_ENC}">
<img id="vec64" src="data:image/svg+xml;base64,${SVG_B64}">
<img id="noise" src="${NOISE}">
<div id="bg" style="width:20px;height:20px"></div>
<canvas id="c" width="240" height="240"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — data URL images", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("decodes SVG data URLs in <img>, drawImage and CSS backgrounds", () => {
    const { value, stderr } = harness.compileAndRun(
      "data-url-svg",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "data-url", width: 500, height: 400 });
      win.on("ready", () => {
        const doc: any = document;
        const ctx: any = doc.getElementById("c").getContext("2d");
        const px = (x: number, y: number): string => {
          const d = ctx.getImageData(x, y, 1, 1).data;
          return d[0] + "," + d[1] + "," + d[2] + "," + d[3];
        };
        const clear = (): void => { ctx.clearRect(0, 0, 240, 240); };
        const box = (id: string): string => {
          const r: any = doc.getElementById(id).getBoundingClientRect();
          return r.width + "x" + r.height;
        };

        const png = "data:image/png;base64,${PNG}";
        const svgEnc = "data:image/svg+xml,${SVG_ENC}";
        const svg64 = "data:image/svg+xml;base64,${SVG_B64}";
        const noise = "${NOISE}";

        console.log("vec-box=" + box("vec"));
        console.log("vec64-box=" + box("vec64"));
        console.log("noise-box=" + box("noise"));

        // A raster data URL drawn from its string, without an <img>.
        clear();
        ctx.drawImage(png, 0, 0);
        console.log("png-string=" + px(0, 0) + ":" + px(3, 0));

        // SVG data URLs in both encodings, as a string and as an <img>.
        clear();
        ctx.drawImage(svgEnc, 0, 0);
        console.log("svg-enc-string=" + px(0, 0) + ":" + px(7, 5));

        clear();
        ctx.drawImage(svg64, 0, 0);
        console.log("svg64-string=" + px(0, 0));

        clear();
        ctx.drawImage(svgEnc, 0, 0, 16, 12);
        console.log("svg-scaled=" + px(0, 0) + ":" + px(15, 11));

        clear();
        ctx.drawImage(doc.getElementById("vec"), 0, 0);
        console.log("svg-img=" + px(0, 0));

        clear();
        ctx.drawImage(doc.getElementById("vec64"), 0, 0);
        console.log("svg64-img=" + px(0, 0));

        // The request's feTurbulence document: it decodes (opaque rect), while
        // the filter itself remains outside NanoSVG's subset.
        clear();
        ctx.drawImage(noise, 0, 0, 32, 32);
        console.log("noise-alpha=" + ctx.getImageData(0, 0, 1, 1).data[3]);

        // SVG data URL as a CSS background.
        const bg: any = doc.getElementById("bg");
        bg.style.backgroundImage = 'url("' + svgEnc + '")';
        console.log("bg-css=" + getComputedStyle(bg).backgroundImage.substring(0, 26));
        let hits = 0;
        for (const line of win.paintList().split("\\n")) {
          if (line.indexOf("src=data:image/svg+xml") >= 0) hits++;
        }
        console.log("paint-svg=" + hits);

        // Let one rendered frame decode the background texture (stderr reports
        // a failure), then close.
        requestAnimationFrame(() => {
          requestAnimationFrame(() => {
            win.close();
          });
        });
      });
      win.loadHTML(\`${PAGE}\`);
      run();
      `,
      { autocloseMs: 3000 },
    );

    expect(value("vec-box")).toBe("8x6");
    expect(value("vec64-box")).toBe("8x6");
    expect(value("noise-box")).toBe("180x180");
    expect(value("png-string")).toBe("255,0,0,255:0,0,255,255");
    expect(value("svg-enc-string")).toBe("255,0,0,255:255,0,0,255");
    expect(value("svg64-string")).toBe("255,0,0,255");
    expect(value("svg-scaled")).toBe("255,0,0,255:255,0,0,255");
    expect(value("svg-img")).toBe("255,0,0,255");
    expect(value("svg64-img")).toBe("255,0,0,255");
    expect(Number(value("noise-alpha"))).toBeGreaterThan(0);
    expect(value("bg-css")).toMatch(/^url\(data:image\/svg\+xml/);
    expect(value("paint-svg")).toBe("4");
    expect(stderr).not.toContain("could not decode image");
  });
});
