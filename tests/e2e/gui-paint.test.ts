/**
 * End-to-end tests for the `gui` extension — the paint/display list: backgrounds,
 * borders, shaped text runs and images.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — paint", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("sizes and paints images (intrinsic and explicit)", () => {
    const src = harness.imageSrc(harness.logoPath);
    const { stdout } = harness.compileAndRun(
      "image",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #logo { width: 40px; height: 40px; }
      </style></head><body>
        <img id="logo" src="${src}">
        <img id="natural" src="${src}">
      </body></html>
      \`;

      const win = createWindow({ title: "image", width: 400, height: 300 });
      win.on("ready", () => {
        const logo = win.getBoundingClientRect("#logo");
        console.log("logo=" + logo.width + "x" + logo.height);
        const natural = win.getBoundingClientRect("#natural");
        console.log("natural=" + natural.width + "x" + natural.height);
        console.log(win.paintList().trim());
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    // Explicit CSS size wins; without one the intrinsic 4x4 size is used.
    expect(stdout).toContain("logo=40x40");
    expect(stdout).toContain("natural=4x4");
    expect(stdout).toContain(`image x=0.0 y=0.0 w=40.0 h=40.0 src=${src}`);
    expect(stdout).toContain(`image x=0.0 y=40.0 w=4.0 h=4.0 src=${src}`);
  });

  it("sizes and paints an SVG (intrinsic and explicit)", () => {
    // An SVG is a vector source: the image loader detects it and rasterises it
    // at the drawn size instead of failing like `stb_image` alone would.
    const src = harness.writeAsset(
      "logo.svg",
      `<svg xmlns="http://www.w3.org/2000/svg" width="8" height="6" viewBox="0 0 8 6">` +
        `<rect width="8" height="6" fill="#ff0000"/>` +
        `<circle cx="4" cy="3" r="2" fill="#0000ff"/></svg>`,
    );
    const { stdout, stderr } = harness.compileAndRun(
      "svg",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #scaled { width: 64px; height: 48px; }
      </style></head><body>
        <img id="natural" src="${src}">
        <img id="scaled" src="${src}">
      </body></html>
      \`;

      const win = createWindow({ title: "svg", width: 400, height: 300 });
      win.on("ready", () => {
        const natural = win.getBoundingClientRect("#natural");
        console.log("natural=" + natural.width + "x" + natural.height);
        const scaled = win.getBoundingClientRect("#scaled");
        console.log("scaled=" + scaled.width + "x" + scaled.height);
        console.log(win.paintList().trim());
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    // Intrinsic size comes from the SVG's width/height; the explicit CSS size
    // wins for the second image.
    expect(stdout).toContain("natural=8x6");
    expect(stdout).toContain("scaled=64x48");
    expect(stdout).toContain(`image x=0.0 y=0.0 w=8.0 h=6.0 src=${src}`);
    expect(stdout).toContain(`image x=0.0 y=6.0 w=64.0 h=48.0 src=${src}`);
    // The SVG must actually rasterise, not just lay out.
    expect(stderr).not.toContain("could not decode image");
  });

  it("builds a display list of backgrounds and borders", () => {
    const { value, stdout } = harness.compileAndRun(
      "paint",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; background-color: #123456; }
        #box { width: 100px; height: 40px; background: #ff0000; border: 4px solid #00ff00; border-radius: 8px; }
      </style></head><body>
        <div id="box">x</div>
      </body></html>
      \`;

      const win = createWindow({ title: "paint", width: 800, height: 600 });
      win.on("ready", () => {
        const r = win.getBoundingClientRect("#box");
        console.log("box-rect=" + r.x + "," + r.y + "," + r.width + "," + r.height);
        console.log("paint-count=" + win.paintCount());
        console.log("paint-list-start");
        console.log(win.paintList());
        console.log("paint-list-end");
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    const list = stdout.slice(stdout.indexOf("paint-list-start"), stdout.indexOf("paint-list-end"));
    const occurrences = (needle: string): number => list.split(needle).length - 1;

    // Border box = 100x40 content + 4px border on every side.
    expect(value("box-rect")).toBe("0,0,108,48");
    // body background + #box background + 4 border edges.
    expect(value("paint-count")).toBe("6");
    expect(occurrences("#123456ff")).toBe(1); // body background
    expect(occurrences("#ff0000ff")).toBe(1); // #box background
    expect(occurrences("#00ff00ff")).toBe(4); // four border edges
    // The background rectangle carries the 8px corner radius.
    expect(list).toContain("r=8.0 color=#ff0000ff");
  });

  it("emits shaped text runs into the display list", () => {
    const { stdout } = harness.compileAndRun(
      "text-runs",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #t { font-size: 20px; color: #0000ff; }
      </style></head><body>
        <div id="t">Hi</div>
      </body></html>
      \`;

      const win = createWindow({ title: "text-runs", width: 400, height: 300 });
      win.on("ready", () => {
        const r = win.getBoundingClientRect("#t");
        console.log("rect=" + r.x + "," + r.y + "," + r.width + "," + r.height);
        console.log("paint-list-start");
        console.log(win.paintList());
        console.log("paint-list-end");
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    const list = stdout.slice(stdout.indexOf("paint-list-start"), stdout.indexOf("paint-list-end"));
    // The text fragment carries the resolved font size and colour.
    expect(list).toContain('text x=0.0');
    expect(list).toContain('size=20.0');
    expect(list).toContain('color=#0000ffff "Hi"');
  });

  it("does not paint an `opacity: 0` box or its subtree", () => {
    const { value, stdout } = harness.compileAndRun(
      "opacity",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; background-color: #123456; }
        #box { width: 30px; height: 10px; background: #ff0000; }
        #panel { width: 40px; height: 20px; background: #00ff00; opacity: 0; }
        #label { color: #ffff00; }
      </style></head><body>
        <div id="box"></div>
        <div id="panel"><div id="label">hidden</div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "opacity", width: 400, height: 300 });
      win.on("ready", () => {
        // A hidden box is still laid out — it just is not painted.
        const panel = win.getBoundingClientRect("#panel");
        console.log("panel-rect=" + panel.x + "," + panel.y + "," + panel.width + "," + panel.height);
        console.log("paint-count=" + win.paintCount());
        console.log("paint-list-start");
        console.log(win.paintList());
        console.log("paint-list-end");
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    const list = stdout.slice(stdout.indexOf("paint-list-start"), stdout.indexOf("paint-list-end"));

    // Layout is unaffected: the 40x20 box still occupies its place.
    expect(value("panel-rect")).toBe("0,10,40,20");
    // body background + #box background only: #panel and its text paint nothing.
    expect(value("paint-count")).toBe("2");
    expect(list).not.toContain("#00ff00ff");
    expect(list).not.toContain("hidden");
    // Anything above 0 is painted in full (there is no partial compositing yet).
    expect(list).toContain("#ff0000ff");
  });

  it("applies fractional `opacity` to every painted colour in the subtree", () => {
    const { value, stdout } = harness.compileAndRun(
      "opacity-half",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; background-color: #000000; }
        #half { width: 40px; height: 20px; background: #ff0000; opacity: 0.5; }
        #nested { width: 20px; height: 10px; background: #00ff00; color: #00ff00; opacity: 0.5; }
        #label { color: #ffff00; }
      </style></head><body>
        <div id="half"><div id="nested"><span id="label">faded</span></div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "opacity-half", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("opacity=" + win.computedStyle("#nested", "opacity"));
        console.log("list-start");
        console.log(win.paintList());
        console.log("list-end");
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    const list = stdout.slice(stdout.indexOf("list-start"), stdout.indexOf("list-end"));
    expect(value("opacity")).toBe("0.5");
    // #half's red background, then #nested's 0.5 on top of it, then the text's
    // inherited 0.25: alpha is multiplied down the paint chain instead of
    // treating any value above zero as opaque.
    expect(list).toContain("color=#ff000080");
    expect(list).toContain("color=#00ff0040");
    expect(list).toContain("color=#ffff0040");
  });

  it("wraps `mix-blend-mode` boxes in a compositing layer", () => {
    const { value, stdout, stderr } = harness.compileAndRun(
      "mix-blend-mode",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #card { width: 100px; height: 100px; background: #ffffff; }
        #multiply { width: 50px; height: 50px; background: #ff0000; mix-blend-mode: multiply; }
        #overlay { width: 20px; height: 20px; background: #00ff00; mix-blend-mode: overlay; }
      </style></head><body>
        <div id="card"><div id="multiply"><div id="overlay"></div></div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "mix-blend-mode", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("multiply=" + win.computedStyle("#multiply", "mix-blend-mode"));
        console.log("overlay=" + win.computedStyle("#overlay", "mix-blend-mode"));
        console.log("list-start");
        console.log(win.paintList());
        console.log("list-end");
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    const list = stdout.slice(stdout.indexOf("list-start"), stdout.indexOf("list-end"));
    expect(value("multiply")).toBe("multiply");
    expect(value("overlay")).toBe("overlay");
    // The card paints normally; the multiply element and its overlay child are
    // separate nested groups (the renderer composites each as a unit).
    expect(list).toContain("layer blend=multiply");
    expect(list).toContain("layer blend=overlay");
    expect(list).toContain("end-layer");
    expect(list).toContain("color=#ffffffff");
    expect(list).toContain("color=#ff0000ff");
    expect(list).toContain("color=#00ff00ff");
    // The offscreen blend pipeline and its runtime-compiled shader must not
    // fall back on this GPU backend.
    expect(stderr).not.toContain("mix-blend-mode");
  });
});
