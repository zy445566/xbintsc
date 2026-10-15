/**
 * End-to-end tests for the `gui` extension — the layout/paint features added in
 * this milestone: margin collapsing, flex wrapping, z-index, overflow clipping
 * and floats.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

/** The shared `rect(selector)` helper embedded in each test program. */
const RECT_HELPER = `
  const rect = (selector) => {
    const r = win.getBoundingClientRect(selector);
    return r.x + "," + r.y + "," + r.width + "," + r.height;
  };
`;

describe.skipIf(!guiAvailable)("gui extension — advanced layout/paint", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("collapses adjoining vertical margins between siblings", () => {
    const { value } = harness.compileAndRun(
      "margin",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        .a { height: 20px; margin-bottom: 30px; }
        .b { height: 20px; margin-top: 10px; margin-bottom: 25px; }
        .c { height: 20px; margin-top: 5px; margin-bottom: 30px; }
        .d { height: 20px; margin-top: 15px; }
      </style></head><body>
        <div class="a" id="a"></div>
        <div class="b" id="b"></div>
        <div class="c" id="c"></div>
        <div class="d" id="d"></div>
      </body></html>
      \`;

      const win = createWindow({ title: "margin", width: 400, height: 300 });
      win.on("ready", () => {
        ${RECT_HELPER}
        for (const id of ["a", "b", "c", "d"]) console.log(id + "=" + rect("#" + id));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // a ends at 20; collapse(30, 10) = 30, so b starts at 50 (not 60).
    expect(value("a")).toBe("0,0,400,20");
    expect(value("b")).toBe("0,50,400,20");
    // b ends at 70; collapse(25, 5) = 25, so c starts at 95 (not 100).
    expect(value("c")).toBe("0,95,400,20");
    // c ends at 115; collapse(30, 15) = 30, so d starts at 145.
    expect(value("d")).toBe("0,145,400,20");
  });

  it("counts a last child's bottom margin in the parent's height", () => {
    const { value } = harness.compileAndRun(
      "margin-parent",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #outer { padding-bottom: 0; }
        #inner { height: 20px; margin-bottom: 25px; }
        #after { height: 10px; }
      </style></head><body>
        <div id="outer"><div id="inner"></div></div>
        <div id="after"></div>
      </body></html>
      \`;

      const win = createWindow({ title: "margin-parent", width: 400, height: 300 });
      win.on("ready", () => {
        ${RECT_HELPER}
        console.log("outer=" + rect("#outer"));
        console.log("after=" + rect("#after"));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // The inner box ends at 20; its 25px bottom margin stays inside the outer
    // (parent/child bottom-margin collapsing is not implemented), so the outer
    // is 45 tall and #after starts at 45.
    expect(value("outer")).toBe("0,0,400,45");
    expect(value("after")).toBe("0,45,400,10");
  });

  it("wraps flex lines when they exceed the main size", () => {
    const { value } = harness.compileAndRun(
      "flex-wrap",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #row { display: flex; flex-wrap: wrap; width: 100px; }
        #row div { width: 40px; height: 10px; }
      </style></head><body>
        <div id="row">
          <div id="a"></div><div id="b"></div>
          <div id="c"></div><div id="d"></div>
        </div>
      </body></html>
      \`;

      const win = createWindow({ title: "flex-wrap", width: 400, height: 300 });
      win.on("ready", () => {
        ${RECT_HELPER}
        for (const id of ["a", "b", "c", "d"]) console.log(id + "=" + rect("#" + id));
        console.log("row=" + rect("#row"));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // 40 + 40 fits on line 1; c and d wrap onto line 2.
    expect(value("a")).toBe("0,0,40,10");
    expect(value("b")).toBe("40,0,40,10");
    expect(value("c")).toBe("0,10,40,10");
    expect(value("d")).toBe("40,10,40,10");
    // Two 10px lines make the container 20px tall.
    expect(value("row")).toBe("0,0,100,20");
  });

  it("honours `flex-wrap: wrap-reverse`", () => {
    const { value } = harness.compileAndRun(
      "flex-wrap-reverse",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #row { display: flex; flex-wrap: wrap-reverse; align-content: flex-start; width: 100px; height: 40px; }
        #row div { width: 40px; height: 10px; }
      </style></head><body>
        <div id="row">
          <div id="a"></div><div id="b"></div>
          <div id="c"></div><div id="d"></div>
        </div>
      </body></html>
      \`;

      const win = createWindow({ title: "wrap-reverse", width: 400, height: 300 });
      win.on("ready", () => {
        ${RECT_HELPER}
        for (const id of ["a", "b", "c", "d"]) console.log(id + "=" + rect("#" + id));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // The cross axis is flipped: the first line (a, b) sits below the second.
    expect(value("a")).toBe("0,30,40,10");
    expect(value("b")).toBe("40,30,40,10");
    expect(value("c")).toBe("0,20,40,10");
    expect(value("d")).toBe("40,20,40,10");
  });

  it("paints `z-index` siblings in stacking order", () => {
    const { stdout, value } = harness.compileAndRun(
      "zindex",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        .box { position: absolute; top: 0; left: 0; width: 50px; height: 50px; }
        #green { background: #00ff00; z-index: 2; }
        #red { background: #ff0000; z-index: 1; }
      </style></head><body>
        <div class="box" id="green"></div>
        <div class="box" id="red"></div>
      </body></html>
      \`;

      const win = createWindow({ title: "zindex", width: 400, height: 300 });
      win.on("ready", () => {
        const list = win.paintList();
        console.log("order-start");
        console.log(list);
        console.log("order-end");
        console.log("red-before-green=" + (list.indexOf("#ff0000ff") < list.indexOf("#00ff00ff")));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("red-before-green")).toBe("true");
    // The paint list still carries both backgrounds.
    expect(stdout).toContain("#ff0000ff");
    expect(stdout).toContain("#00ff00ff");
  });

  it("clips a subtree with `overflow: hidden`", () => {
    const { value, stdout } = harness.compileAndRun(
      "overflow",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #clip { width: 50px; height: 50px; overflow: hidden; }
        #inner { width: 200px; height: 20px; background: #00ff00; }
      </style></head><body>
        <div id="clip"><div id="inner"></div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "overflow", width: 400, height: 300 });
      win.on("ready", () => {
        const r = win.getBoundingClientRect("#inner");
        console.log("inner-rect=" + r.x + "," + r.y + "," + r.width + "," + r.height);
        console.log("paint-list-start");
        console.log(win.paintList());
        console.log("paint-list-end");
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    const list = stdout.slice(stdout.indexOf("paint-list-start"), stdout.indexOf("paint-list-end"));
    // Clipping does not change layout: the inner box is still 200px wide.
    expect(value("inner-rect")).toBe("0,0,200,20");
    // Its background is drawn inside the 50x50 padding-box clip.
    expect(list).toContain("clip=0.0,0.0,50.0,50.0 rect x=0.0 y=0.0 w=200.0 h=20.0");
  });

  it("wraps inline content around a left float", () => {
    const { value, stdout } = harness.compileAndRun(
      "float",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #w { width: 100px; font-size: 20px; }
        #f { float: left; width: 40px; height: 40px; background: #ff0000; }
        #after { clear: both; height: 10px; }
      </style></head><body>
        <div id="w"><div id="f"></div>hello world foo bar baz<div id="after"></div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "float", width: 400, height: 300 });
      win.on("ready", () => {
        const f = win.getBoundingClientRect("#f");
        console.log("float=" + f.x + "," + f.y + "," + f.width + "," + f.height);
        const w = win.getBoundingClientRect("#w");
        console.log("w-height=" + w.height);
        console.log("paint-list-start");
        console.log(win.paintList());
        console.log("paint-list-end");
        const a = win.getBoundingClientRect("#after");
        console.log("after-y=" + a.y);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    // The float sits at the left edge and is 40x40.
    expect(value("float")).toBe("0,0,40,40");
    // The container grows to contain the float.
    expect(Number(value("w-height"))).toBeGreaterThanOrEqual(40);
    const list = stdout.slice(stdout.indexOf("paint-list-start"), stdout.indexOf("paint-list-end"));
    // The first text line is pushed right of the float.
    expect(list).toContain("text x=40.0");
    // `clear: both` pushes the following block below the float.
    expect(Number(value("after-y"))).toBeGreaterThanOrEqual(40);
  });
});
