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
    const { stdout } = harness.compileAndRun(
      "image",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #logo { width: 40px; height: 40px; }
      </style></head><body>
        <img id="logo" src="${harness.logoPath}">
        <img id="natural" src="${harness.logoPath}">
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
    expect(stdout).toContain(`image x=0.0 y=0.0 w=40.0 h=40.0 src=${harness.logoPath}`);
    expect(stdout).toContain(`image x=0.0 y=40.0 w=4.0 h=4.0 src=${harness.logoPath}`);
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
});
