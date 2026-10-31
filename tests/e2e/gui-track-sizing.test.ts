/**
 * End-to-end tests for the flex/grid track sizing that the GUI engine used to
 * leave on the floor: `fr` rows, and the column flex basis/shrink/cross-axis
 * alignment paths.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — track sizing", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("divides a definite grid height across fr rows", () => {
    const { value } = harness.compileAndRun(
      "grid-fr-rows",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #grid {
          position: absolute; left: 0; top: 0; width: 160px; height: 200px;
          display: grid; grid-template-columns: repeat(2, 1fr);
          grid-template-rows: repeat(4, 1fr); gap: 10px;
        }
        #grid span { border: 1px solid #000; }
        #ar {
          position: absolute; left: 200px; top: 0; width: 120px;
          aspect-ratio: 1 / 2; display: grid;
          grid-template-rows: repeat(3, 1fr);
        }
        #ar span { border: 1px solid #000; }
      </style></head><body>
        <div id="grid">
          <span id="g0"></span><span id="g1"></span>
          <span id="g2"></span><span id="g3"></span>
          <span id="g4"></span><span id="g5"></span>
          <span id="g6"></span><span id="g7"></span>
        </div>
        <div id="ar"><span id="a0"></span><span id="a1"></span><span id="a2"></span></div>
      </body></html>
      \`;

      const win = createWindow({ title: "grid-fr-rows", width: 400, height: 300 });
      win.on("ready", () => {
        const rect = (selector) => {
          const r = win.getBoundingClientRect(selector);
          return r.x + "," + r.y + "," + r.width + "," + r.height;
        };
        for (const id of ["g0", "g2", "a0", "a1"]) console.log(id + "=" + rect("#" + id));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    const [g0x, g0y, g0w, g0h] = value("g0").split(",").map(Number);
    expect(g0x).toBeCloseTo(0, 3);
    expect(g0y).toBeCloseTo(0, 3);
    expect(g0w).toBeCloseTo(75, 3);
    // (200 - 3 * 10) / 4 = 42.5; without the row pass this was the 2px border.
    expect(g0h).toBeCloseTo(42.5, 3);
    expect(Number(value("g2").split(",")[1])).toBeCloseTo(52.5, 3);

    const [a0x, a0y, a0w, a0h] = value("a0").split(",").map(Number);
    expect(a0x).toBeCloseTo(200, 3);
    expect(a0y).toBeCloseTo(0, 3);
    expect(a0w).toBeCloseTo(120, 3);
    // `aspect-ratio: 1 / 2` makes the grid 240px tall, so each row is 80px.
    expect(a0h).toBeCloseTo(80, 3);
    expect(Number(value("a1").split(",")[1])).toBeCloseTo(80, 3);
  });

  it("honors flex-basis and flex-shrink in a column", () => {
    const { value } = harness.compileAndRun(
      "flex-column-sizing",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #basis {
          position: absolute; left: 0; top: 0; width: 160px; height: 200px;
          display: flex; flex-direction: column; gap: 10px;
        }
        #basis div { flex: 0 0 40px; }
        #shrink {
          position: absolute; left: 200px; top: 0; width: 160px; height: 120px;
          display: flex; flex-direction: column;
        }
        #shrink div { height: 50px; }
      </style></head><body>
        <div id="basis"><div id="b0"></div><div id="b1"></div><div id="b2"></div><div id="b3"></div></div>
        <div id="shrink"><div id="s0"></div><div id="s1"></div><div id="s2"></div><div id="s3"></div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "flex-column-sizing", width: 400, height: 300 });
      win.on("ready", () => {
        const rect = (selector) => {
          const r = win.getBoundingClientRect(selector);
          return r.x + "," + r.y + "," + r.width + "," + r.height;
        };
        for (const id of ["b0", "b1", "s0", "s1", "s3"]) console.log(id + "=" + rect("#" + id));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // `flex: 0 0 40px` sets the main size outright; the next item starts after
    // the basis plus the 10px gap.
    const b0 = value("b0").split(",").map(Number);
    expect(b0[3]).toBeCloseTo(40, 3);
    expect(Number(value("b1").split(",")[1])).toBeCloseTo(50, 3);

    // Four 50px items in 120px shrink equally: 50 - 80 * 50 / 200 = 30.
    for (const id of ["s0", "s1", "s3"]) {
      const rect = value(id).split(",").map(Number);
      expect(rect[3]).toBeCloseTo(30, 3);
    }
    expect(Number(value("s1").split(",")[1])).toBeCloseTo(30, 3);
    expect(Number(value("s3").split(",")[1])).toBeCloseTo(90, 3);
  });

  it("applies column cross-axis alignment and align-self", () => {
    const { value } = harness.compileAndRun(
      "flex-cross-alignment",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #colAlign {
          position: absolute; left: 0; top: 0; width: 200px; height: 100px;
          display: flex; flex-direction: column; align-items: center;
        }
        #colAlign div { width: 40px; height: 20px; }
        #colSelf {
          position: absolute; left: 220px; top: 0; width: 200px; height: 100px;
          display: flex; flex-direction: column; align-items: flex-start;
        }
        #colSelf div { width: 40px; height: 20px; }
        #colSelf #target { align-self: flex-end; }
        #rowSelf {
          position: absolute; left: 0; top: 150px; width: 200px; height: 100px;
          display: flex; align-items: flex-start;
        }
        #rowSelf div { width: 20px; height: 20px; }
        #rowSelf #rtarget { align-self: center; }
      </style></head><body>
        <div id="colAlign"><div id="ca0"></div></div>
        <div id="colSelf"><div id="cs0"></div><div id="target"></div></div>
        <div id="rowSelf"><div id="rs0"></div><div id="rtarget"></div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "flex-cross-alignment", width: 500, height: 300 });
      win.on("ready", () => {
        const rect = (selector) => {
          const r = win.getBoundingClientRect(selector);
          return r.x + "," + r.y + "," + r.width + "," + r.height;
        };
        for (const id of ["ca0", "cs0", "target", "rs0", "rtarget"]) {
          console.log(id + "=" + rect("#" + id));
        }
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // 40px item in a 200px column: (200 - 40) / 2 = 80.
    expect(Number(value("ca0").split(",")[0])).toBeCloseTo(80, 3);
    // `align-self: flex-end` overrides the container's flex-start; the
    // container itself is at left: 220px.
    expect(Number(value("cs0").split(",")[0])).toBeCloseTo(220, 3);
    expect(Number(value("target").split(",")[0])).toBeCloseTo(380, 3);
    // Row axis: 20px item in a 100px line, from `top: 150px`.
    expect(Number(value("rs0").split(",")[1])).toBeCloseTo(150, 3);
    expect(Number(value("rtarget").split(",")[1])).toBeCloseTo(190, 3);
  });
});
