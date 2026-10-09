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

  it("positions boxes with `position` and its offsets", () => {
    const { value } = harness.compileAndRun(
      "position",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        .rel { position: relative; top: -20px; left: 15px; height: 30px; }
        .flow { height: 50px; }
        #stage { position: absolute; top: 40px; left: 0px; right: 0px; height: 300px; }
        #stage .inner { position: absolute; top: 30px; left: 40px; width: 50px; height: 20px; }
        #stage .pinned { position: absolute; right: 10px; bottom: 20px; width: 60px; height: 30px; }
        #stage .filler { height: 25px; }
        #stage .stretch { position: absolute; left: 0px; right: 0px; top: 200px; height: 10px; }
        #stage .auto { position: absolute; left: 10px; bottom: 200px; }
      </style></head><body>
        <div class="flow" id="a"></div>
        <div class="rel" id="b"></div>
        <div class="flow" id="c"></div>
        <div id="stage">
          <div class="inner" id="inner"></div>
          <div class="pinned" id="pinned"></div>
          <div class="filler" id="filler"></div>
          <div class="stretch" id="stretch"></div>
          <div class="auto" id="auto"><span style="font-size: 10px">abcd</span></div>
        </div>
      </body></html>
      \`;

      const win = createWindow({ title: "position", width: 800, height: 600 });
      win.on("ready", () => {
        const rect = (selector) => {
          const r = win.getBoundingClientRect(selector);
          return r.x + "," + r.y + "," + r.width + "," + r.height;
        };
        for (const id of ["a", "b", "c", "stage", "inner", "pinned", "filler", "stretch", "auto"]) {
          console.log(id + "=" + rect("#" + id));
        }
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // `relative` moves the box without changing the space it reserved, so the
    // following sibling advances by the *unshifted* height: `a` and `b` occupy
    // 50 + 50, and `c` starts at 100 - 20 = 80.
    expect(value("a")).toBe("0,0,800,50");
    expect(value("b")).toBe("15,30,800,30");
    expect(value("c")).toBe("0,80,800,50");
    // The stage fills `left:0/right:0` and starts at `top:40px`.
    expect(value("stage")).toBe("0,40,800,300");
    // Absolute offsets resolve against the stage's padding box (its origin).
    expect(value("inner")).toBe("40,70,50,20");
    // `right`/`bottom` are measured from the stage's far edges: x = 800 - 10 -
    // 60, y = 40 + 300 - 20 - 30.
    expect(value("pinned")).toBe("730,290,60,30");
    // Normal flow inside the stage is not disturbed by the absolute boxes.
    expect(value("filler")).toBe("0,40,800,25");
    // `left` + `right` with `auto` width fills the containing block.
    expect(value("stretch")).toBe("0,240,800,10");
    // `auto` width with only one inset is shrink-to-fit, not full width; the
    // `bottom: 200px` still pins its bottom edge to the containing block.
    const auto = value("auto").split(",").map(Number);
    const autoY = Number(auto[1]);
    const autoW = Number(auto[2]);
    const autoH = Number(auto[3]);
    expect(autoY + autoH).toBe(140);
    expect(autoH).toBeGreaterThan(8);
    expect(autoW).toBeGreaterThan(10);
    expect(autoW).toBeLessThan(60);
  });

  it("keeps margin in flex layout", () => {
    const { value } = harness.compileAndRun(
      "flexmargin",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #col { display: flex; flex-direction: column; height: 200px; }
        #col .one { height: 50px; }
        #col .two { height: 50px; margin-top: -20px; }
        #col .three { height: 50px; margin-left: 30px; }
        #row { display: flex; width: 300px; }
        #row .a { width: 40px; height: 20px; margin-right: 10px; }
        #row .b { width: 40px; height: 20px; margin-top: 15px; }
      </style></head><body>
        <div id="col"><div class="one" id="one"></div><div class="two" id="two"></div><div class="three" id="three"></div></div>
        <div id="row"><div class="a" id="ra"></div><div class="b" id="rb"></div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "flexmargin", width: 800, height: 600 });
      win.on("ready", () => {
        const rect = (selector) => {
          const r = win.getBoundingClientRect(selector);
          return r.x + "," + r.y + "," + r.width + "," + r.height;
        };
        for (const id of ["one", "two", "three", "ra", "rb"]) console.log(id + "=" + rect("#" + id));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // A negative `margin-top` in a flex column pulls the item up (50 -> 30).
    expect(value("one")).toBe("0,0,800,50");
    expect(value("two")).toBe("0,30,800,50");
    // `margin-left` shifts the item; `align-items` defaults to stretch, so the
    // cross-size still fills the column.
    expect(value("three")).toBe("30,80,770,50");
    // In a flex row the margin is part of the item's outer size.
    expect(value("ra")).toBe("0,200,40,20");
    expect(value("rb")).toBe("50,215,40,20");
  });

  it("collapses source whitespace between block boxes", () => {
    const { value } = harness.compileAndRun(
      "whitespace",
      `
      import { createWindow, run } from "gui";

      /* Written the way a real document is: the indentation is whitespace-only
         text between block boxes, which HTML collapses away. */
      const HTML = "<html>\\n" +
        "  <head><style>body { margin: 0; } .box { height: 20px; }</style></head>\\n" +
        "  <body>\\n" +
        "    <div class=\\"box\\" id=\\"one\\"></div>\\n" +
        "    <p id=\\"para\\">hello <span>world</span></p>\\n" +
        "    <div class=\\"box\\" id=\\"two\\"></div>\\n" +
        "  </body>\\n" +
        "</html>";

      const win = createWindow({ title: "whitespace", width: 400, height: 300 });
      win.on("ready", () => {
        const rect = (selector) => {
          const r = win.getBoundingClientRect(selector);
          return r.x + "," + r.y + "," + r.width + "," + r.height;
        };
        for (const id of ["one", "para", "two"]) console.log(id + "=" + rect("#" + id));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // The first block starts at the very top: the newline/indentation before it
    // (and between the boxes) must not become text or an empty line.
    expect(value("one")).toBe("0,0,400,20");
    // A paragraph keeps its own margins, and the whitespace *inside* it is not
    // touched (only whitespace between block boxes collapses).
    const para = value("para").split(",").map(Number);
    const paraTop = Number(para[1]);
    const paraHeight = Number(para[3]);
    expect(para[2]).toBe(400);
    expect(paraTop).toBeGreaterThanOrEqual(20);
    expect(paraHeight).toBeGreaterThan(0);
    // `two` follows the paragraph rather than sitting next to `one`.
    const twoTop = Number(value("two").split(",")[1]);
    expect(twoTop).toBeGreaterThan(paraTop + paraHeight);
  });

  it("shapes and measures text with the font stack", () => {    const { value } = harness.compileAndRun(
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
