/**
 * End-to-end tests for the `gui` extension — CSS `transform` and
 * `transform-origin`: parsing/computed style, the matrix the paint list carries,
 * transformed geometry (`getBoundingClientRect`), hit testing, and animating a
 * transform through a CSS transition.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — transform", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("composes translate/rotate/scale into one matrix", () => {
    const { stdout, value } = harness.compileAndRun(
      "transform-matrix",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #moved {
          position: absolute; left: 100px; top: 100px; width: 100px; height: 50px;
          background-color: #ff0000;
          transform-origin: 0 0;
          transform: rotate(90deg) translate(10px, 0px);
        }
        #grew {
          position: absolute; left: 100px; top: 200px; width: 40px; height: 20px;
          background-color: #00ff00;
          transform: scale(2, 3);
        }
      </style></head><body>
        <div id="moved"></div><div id="grew"></div>
      </body></html>
      \`;

      const win = createWindow({ title: "transform", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("computed=" + win.computedStyle("#moved", "transform"));
        console.log("origin=" + win.computedStyle("#moved", "transform-origin"));
        console.log("default-origin=" + win.computedStyle("#grew", "transform-origin"));
        const moved = win.getBoundingClientRect("#moved");
        console.log("moved=" + moved.x.toFixed(1) + "," + moved.y.toFixed(1) + "," +
                    moved.width.toFixed(1) + "," + moved.height.toFixed(1));
        const grew = win.getBoundingClientRect("#grew");
        console.log("grew=" + grew.x.toFixed(1) + "," + grew.y.toFixed(1) + "," +
                    grew.width.toFixed(1) + "," + grew.height.toFixed(1));
        console.log(win.paintList().trim());
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("computed")).toBe("rotate(90deg) translate(10px, 0px)");
    expect(value("origin")).toBe("0px 0px");
    // `transform-origin` defaults to the centre of the border box.
    expect(value("default-origin")).toBe("50% 50%");
    /* rotate(90deg) about the top-left corner turns the 100x50 box into a
     * 50x100 one; the local `translate(10px, 0)` moves it 10px down. */
    expect(value("moved")).toBe("50.0,110.0,50.0,100.0");
    // `scale(2, 3)` about the centre: 40x20 -> 80x60, centred on the same point.
    expect(value("grew")).toBe("80.0,180.0,80.0,60.0");
    // Paint keeps the untransformed layout geometry and carries the matrix.
    expect(stdout).toContain("rect x=100.0 y=100.0 w=100.0 h=50.0 r=0.0 color=#ff0000ff transform=matrix(");
    expect(stdout).toContain("transform=matrix(2,0,0,3,-120,-420)");
  });

  it("parses the 3d spellings and drops unsupported functions", () => {
    const { stdout, value } = harness.compileAndRun(
      "transform-parse",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #a {
          position: absolute; left: 0px; top: 0px; width: 40px; height: 20px;
          background-color: #ff0000; transform-origin: 0 0;
          transform: translate3d(10px, 4px, 0) scale3d(2, 3, 1) rotateZ(0.5turn);
        }
        #b {
          position: absolute; left: 0px; top: 100px; width: 40px; height: 20px;
          background-color: #00ff00; transform: skewX(10deg);
        }
        #c {
          position: absolute; left: 100px; top: 100px; width: 100px; height: 50px;
          background-color: #0000ff; transform-origin: 0 0;
          transform: translate(50%, 50%);
        }
        #d { transform-origin: top; }
        #e { transform-origin: bottom right; }
        #f { transform-origin: 10px; }
      </style></head><body><div id="a"></div><div id="b"></div><div id="c"></div>
      <div id="d"></div><div id="e"></div><div id="f"></div></body></html>
      \`;

      const win = createWindow({ title: "parse", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("a=" + win.computedStyle("#a", "transform"));
        console.log("b=" + win.computedStyle("#b", "transform"));
        console.log("c=" + win.computedStyle("#c", "transform"));
        /* translate(50%, 50%) resolves against the box: 50px, 25px. */
        console.log("c-rect=" + win.getBoundingClientRect("#c").x.toFixed(1) + "," +
                    win.getBoundingClientRect("#c").y.toFixed(1));
        /* A vertical keyword keeps x centred; bottom-right is right-bottom. */
        console.log("d-origin=" + win.computedStyle("#d", "transform-origin"));
        console.log("e-origin=" + win.computedStyle("#e", "transform-origin"));
        console.log("f-origin=" + win.computedStyle("#f", "transform-origin"));
        console.log(win.paintList().trim());
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    // `translate3d`/`scale3d` keep their x/y and drop `z`; `rotateZ` is `rotate`.
    expect(value("a")).toBe("translate(10px, 4px) scale(2, 3) rotate(180deg)");
    // An unsupported function invalidates the whole declaration.
    expect(value("b")).toBe("none");
    // Percentages stay unresolved in the computed value and resolve in layout.
    expect(value("c")).toBe("translate(50%, 50%)");
    expect(value("c-rect")).toBe("150.0,125.0");
    expect(value("d-origin")).toBe("50% 0%");
    expect(value("e-origin")).toBe("100% 100%");
    expect(value("f-origin")).toBe("10px 50%");
    expect(stdout).toContain("transform=matrix(1,0,0,1,50,25)");
  });

  it("hit tests through the transform", () => {
    const { value } = harness.compileAndRun(
      "transform-hit",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box {
          position: absolute; left: 100px; top: 100px; width: 100px; height: 50px;
          background-color: #ff0000;
          transform-origin: 0 0;
          transform: rotate(90deg);
        }
      </style></head><body><div id="box"></div></body></html>
      \`;

      const win = createWindow({ title: "hit", width: 400, height: 300 });
      win.on("ready", () => {
        /* The rotated box covers x 50..150, y 100..200 and no longer covers the
         * area it occupied before the rotation (x 100..200). */
        console.log("rotated=" + win.hitTest(60, 190));
        console.log("vacated=" + win.hitTest(190, 110));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("rotated")).toBe("div#box");
    expect(value("vacated")).toBe("");
  });

  it("animates `transform` through a CSS transition", () => {
    const { value } = harness.compileAndRun(
      "transform-transition",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box {
          position: absolute; left: 20px; top: 200px; width: 60px; height: 30px;
          background-color: #0000ff;
          transform: translateX(0px) rotate(0deg);
          transition: transform 400ms linear;
        }
        #box:hover { transform: translateX(40px) rotate(90deg); }
      </style></head><body><div id="box"></div></body></html>
      \`;

      const win = createWindow({ title: "transition", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("start=" + win.computedStyle("#box", "transform"));
        win.sendEvent("mousemove", { x: 50, y: 215 });
        win.advance(100);
        console.log("q1=" + win.computedStyle("#box", "transform"));
        win.advance(200);
        console.log("q3=" + win.computedStyle("#box", "transform"));
        win.advance(200);
        console.log("end=" + win.computedStyle("#box", "transform"));
        const end = win.getBoundingClientRect("#box");
        console.log("end-rect=" + end.x.toFixed(1) + "," + end.y.toFixed(1) + "," +
                    end.width.toFixed(1) + "," + end.height.toFixed(1));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("start")).toBe("translateX(0px) rotate(0deg)");
    // 100/400 and 300/400 of the way between the two lists, linearly.
    expect(value("q1")).toBe("translateX(10px) rotate(22.5deg)");
    expect(value("q3")).toBe("translateX(30px) rotate(67.5deg)");
    expect(value("end")).toBe("translateX(40px) rotate(90deg)");
    // Moved 40px right and rotated a quarter turn about its centre.
    expect(value("end-rect")).toBe("75.0,185.0,30.0,60.0");
  });
});
