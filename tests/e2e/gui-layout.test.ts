/**
 * End-to-end tests for the `gui` extension — box layout and text measurement.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — layout", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("lays out block, inline and flex boxes", () => {
    const { value } = harness.compileAndRun(
      "layout",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box { width: 200px; height: 100px; padding: 10px; border: 2px solid #000; margin: 5px; }
        #row { display: flex; width: 300px; gap: 10px; }
        #row .a { width: 50px; height: 20px; }
        #row .b { flex: 1; height: 20px; }
        #wrap { width: 60px; font-size: 10px; }
      </style></head><body>
        <div id="box">hello</div>
        <div id="row"><div class="a"></div><div class="b"></div></div>
        <div id="wrap">aaaa bbbb cccc dddd</div>
      </body></html>
      \`;

      const win = createWindow({ title: "layout", width: 800, height: 600 });
      win.on("ready", () => {
        const rect = (selector) => {
          const r = win.getBoundingClientRect(selector);
          return r.x + "," + r.y + "," + r.width + "," + r.height;
        };
        console.log("box=" + rect("#box"));
        console.log("a=" + rect("#row .a"));
        console.log("b=" + rect("#row .b"));
        console.log("wrap=" + rect("#wrap"));
        console.log("lh=" + win.fontMetrics(10).lineHeight);
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // Border box = content + padding (20) + border (4); margin offsets by 5.
    expect(value("box")).toBe("5,5,224,124");
    // Flex row: `.a` keeps 50px, `.b` grows into the remaining 240px after a 10px gap.
    expect(value("a")).toBe("0,134,50,20");
    expect(value("b")).toBe("60,134,240,20");
    // 60px-wide box wraps "aaaa bbbb" / "cccc dddd" onto two lines. The line
    // height comes from the real font metrics, so assert against them rather
    // than hard-coding a pixel value (fonts differ per platform).
    const wrap = value("wrap").split(",").map(Number);
    expect(wrap.slice(0, 3)).toEqual([0, 154, 60]);
    expect(wrap[3]).toBeCloseTo(2 * Number(value("lh")), 1);
  });

  it("shapes and measures text with the font stack", () => {
    const { value } = harness.compileAndRun(
      "text",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "text", width: 400, height: 300 });
      win.on("ready", () => {
        const fm16 = win.fontMetrics(16);
        console.log("ready=" + fm16.ready);
        console.log("lh16=" + fm16.lineHeight);
        console.log("ascent16=" + fm16.ascent);
        console.log("m16=" + win.measureText("Hello", 16));
        console.log("m32=" + win.measureText("Hello", 32));
        console.log("empty=" + win.measureText("", 16));
        console.log("wide=" + (win.measureText("WWWW", 16) > win.measureText("iiii", 16)));
        win.close();
      });
      win.loadHTML("<html><body></body></html>");
      run();
      `,
    );

    // Metrics are positive and scale ~linearly with the font size (this holds
    // for both the real shaper and the no-font fallback).
    const m16 = Number(value("m16"));
    const m32 = Number(value("m32"));
    expect(m16).toBeGreaterThan(0);
    expect(m32).toBeGreaterThan(m16 * 1.8);
    expect(m32).toBeLessThan(m16 * 2.2);
    expect(Number(value("lh16"))).toBeGreaterThan(0);
    expect(Number(value("ascent16"))).toBeGreaterThan(0);
    expect(value("empty")).toBe("0");
    expect(value("wide")).toBe("true");
  });
});
