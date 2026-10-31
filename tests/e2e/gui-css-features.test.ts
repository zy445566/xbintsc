/**
 * End-to-end tests for the CSS features that need more than a value parser:
 * `calc()`, `box-shadow`, Gaussian `text-shadow`, `backdrop-filter`, CSS
 * transitions (arbitrary properties + cubic-bezier/steps) and `@keyframes`.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — CSS features", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("evaluates calc() arithmetic in every length property", () => {
    const { value } = harness.compileAndRun(
      "calc",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #a {
          position: absolute;
          box-sizing: border-box;
          font-size: calc(8px + 2px);
          left: calc(10px + 5%);
          top: calc(1em + 4px);
          width: calc(100px + 2 * 10px);
          height: calc(50px - 5px);
          padding-left: calc(1em * 2 - 4px);
          background: #ff0000;
        }
        #b { width: min(calc(100px + 10px), 200px); height: clamp(10px, calc(2 * 5px), 40px); }
      </style></head><body><div id="a"></div><div id="b"></div></body></html>
      \`;

      const win = createWindow({ title: "calc", width: 400, height: 300 });
      win.on("ready", () => {
        const a = win.getBoundingClientRect("#a");
        console.log("a=" + a.x.toFixed(1) + "," + a.y.toFixed(1) + "," +
                    a.width.toFixed(1) + "," + a.height.toFixed(1));
        console.log("font=" + win.computedStyle("#a", "font-size"));
        console.log("width=" + win.computedStyle("#a", "width"));
        const b = win.getBoundingClientRect("#b");
        console.log("b=" + b.width.toFixed(1) + "," + b.height.toFixed(1));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    // 5% of the 400px viewport is 20px; 1em is the calc'd 10px font size.
    expect(value("a")).toBe("30.0,14.0,120.0,45.0");
    expect(value("font")).toBe("10px");
    expect(value("width")).toBe("calc(100px + 2 * 10px)");
    expect(value("b")).toBe("110.0,10.0");
  });

  it("paints box-shadow and blurred text-shadow as Gaussian masks", () => {
    const { value, stdout } = harness.compileAndRun(
      "shadows",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box {
          width: 100px; height: 50px; background: #336699;
          box-shadow: 4px 6px 12px 2px rgba(0, 0, 0, 0.5), inset 0 0 8px #ff0000;
        }
        #text { font-size: 20px; color: #ffffff; text-shadow: 2px 3px 6px #00ff00; }
      </style></head><body><div id="box"></div><div id="text">Hi</div></body></html>
      \`;

      const win = createWindow({ title: "shadows", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("box=" + win.computedStyle("#box", "box-shadow"));
        console.log("text=" + win.computedStyle("#text", "text-shadow"));
        console.log("list-start");
        console.log(win.paintList());
        console.log("list-end");
        let frames = 0;
        const tick = (): void => {
          frames += 1;
          if (frames < 2) win.requestAnimationFrame(tick);
          else win.close();
        };
        win.requestAnimationFrame(tick);
      });
      win.loadHTML(HTML);
      run();
      `,
      { autocloseMs: 10_000 },
    );
    const list = stdout.slice(stdout.indexOf("list-start"), stdout.indexOf("list-end"));
    expect(value("box")).toContain("inset 0px 0px 8px");
    expect(value("box")).toContain("4px 6px 12px 2px");
    expect(value("text")).toBe("2px 3px 6px rgb(0, 255, 0)");
    // A nonzero blur is one pre-blurred texture, not a stack of offset copies.
    expect(list).toContain("bitmap=bs:o:");
    expect(list).toContain("bitmap=bs:i:");
    expect(list).toContain("bitmap=ts:");
  });

  it("runs @keyframes with direction, timing and fill modes", () => {
    const { value } = harness.compileAndRun(
      "keyframes",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        @keyframes fade {
          from { opacity: 0; transform: translateX(0px); }
          50% { opacity: 1; }
          to { opacity: 0; transform: translateX(100px); }
        }
        #k { width: 20px; height: 20px; background: #000000;
             animation: fade 1000ms linear forwards; }
      </style></head><body><div id="k"></div></body></html>
      \`;

      const win = createWindow({ title: "keyframes", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("t0=" + win.computedStyle("#k", "opacity"));
        win.advance(250);
        console.log("t25=" + win.computedStyle("#k", "opacity"));
        console.log("x25=" + win.computedStyle("#k", "transform"));
        win.advance(250);
        console.log("t50=" + win.computedStyle("#k", "opacity"));
        win.advance(250);
        console.log("t75=" + win.computedStyle("#k", "opacity"));
        console.log("x75=" + win.computedStyle("#k", "transform"));
        win.advance(250);
        console.log("t100=" + win.computedStyle("#k", "opacity"));
        win.advance(1000);
        console.log("held=" + win.computedStyle("#k", "opacity"));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("t0")).toBe("0");
    expect(value("t25")).toBe("0.5");
    expect(value("x25")).toBe("translateX(25px)");
    expect(value("t50")).toBe("1");
    expect(value("t75")).toBe("0.5");
    expect(value("x75")).toBe("translateX(75px)");
    expect(value("t100")).toBe("0");
    // `forwards` holds the final keyframe instead of snapping back.
    expect(value("held")).toBe("0");
  });

  it("honours animation delay, backwards fill and alternate direction", () => {
    const { value } = harness.compileAndRun(
      "keyframes-delay",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        @keyframes fade { from { opacity: 0; } to { opacity: 1; } }
        #d { animation: fade 1000ms linear 500ms backwards; }
        #e { animation: fade 1000ms linear infinite alternate; }
      </style></head><body><div id="d"></div><div id="e"></div></body></html>
      \`;

      const win = createWindow({ title: "keyframes-delay", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("d0=" + win.computedStyle("#d", "opacity"));
        console.log("e0=" + win.computedStyle("#e", "opacity"));
        win.advance(500);
        console.log("d500=" + win.computedStyle("#d", "opacity"));
        win.advance(500);
        console.log("d1000=" + win.computedStyle("#d", "opacity"));
        console.log("e1000=" + win.computedStyle("#e", "opacity"));
        win.advance(500);
        console.log("e1500=" + win.computedStyle("#e", "opacity"));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    // `backwards` shows the first keyframe throughout the delay.
    expect(value("d0")).toBe("0");
    expect(value("d500")).toBe("0");
    expect(value("d1000")).toBe("0.5");
    // `alternate` starts the second iteration at the first one's end value
    // and then runs it backwards.
    expect(value("e0")).toBe("0");
    expect(value("e1000")).toBe("1");
    expect(value("e1500")).toBe("0.5");
  });

  it("interpolates transitions with arbitrary properties, cubic-bezier and steps", () => {
    const { value } = harness.compileAndRun(
      "transition-easing",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #t {
          width: 100px; height: 20px; opacity: 1;
          background: #000000;
          transition: width 1000ms steps(4, end),
                      opacity 1000ms cubic-bezier(0.5, 0, 0.5, 1);
        }
        #t:hover { width: 200px; opacity: 0.2; }
      </style></head><body><div id="t"></div></body></html>
      \`;

      const win = createWindow({ title: "transition-easing", width: 400, height: 300 });
      win.on("ready", () => {
        win.sendEvent("mousemove", { x: 50, y: 10 });
        console.log("start=" + win.getBoundingClientRect("#t").width.toFixed(1));
        win.advance(200);
        console.log("w20=" + win.getBoundingClientRect("#t").width.toFixed(1));
        win.advance(300);
        console.log("w50=" + win.getBoundingClientRect("#t").width.toFixed(1));
        console.log("o50=" + win.computedStyle("#t", "opacity"));
        win.advance(300);
        console.log("w80=" + win.getBoundingClientRect("#t").width.toFixed(1));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    // steps(4, end) holds 100px until 250ms, then jumps by 25px.
    expect(value("start")).toBe("100.0");
    expect(value("w20")).toBe("100.0");
    expect(value("w50")).toBe("150.0");
    // cubic-bezier(0.5, 0, 0.5, 1) is symmetric: 50% progress -> 0.6 opacity.
    expect(value("o50")).toBe("0.6");
    expect(value("w80")).toBe("175.0");
  });

  it("composites backdrop-filter from an offscreen snapshot of the scene", () => {
    const { value, stdout, stderr } = harness.compileAndRun(
      "backdrop",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #scene { width: 200px; height: 100px; background: #3366cc; }
        #glass {
          position: absolute; left: 20px; top: 20px; width: 80px; height: 40px;
          border-radius: 8px;
          backdrop-filter: blur(4px) brightness(0.5);
          background: rgba(255, 255, 255, 0.2);
        }
      </style></head><body>
        <div id="scene"><div id="glass"></div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "backdrop", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("filter=" + win.computedStyle("#glass", "backdrop-filter"));
        console.log("list-start");
        console.log(win.paintList());
        console.log("list-end");
        /* Keep the loop alive for a couple of frames: the compositor compiles
         * its shaders on the first draw, which is what exercises the backdrop
         * pass (the assertions read its stderr). */
        let frames = 0;
        const tick = (): void => {
          frames += 1;
          if (frames < 3) win.requestAnimationFrame(tick);
          else win.close();
        };
        win.requestAnimationFrame(tick);
      });
      win.loadHTML(HTML);
      run();
      `,
      { features: ["gpu"], autocloseMs: 10_000 },
    );
    const list = stdout.slice(stdout.indexOf("list-start"), stdout.indexOf("list-end"));
    expect(value("filter")).toBe("blur(4px) brightness(0.5)");
    expect(list).toContain("backdrop x=20.0 y=20.0 w=80.0 h=40.0 r=8.0 blur=4.0");
    // The runtime shader must have compiled; a failure would only log.
    expect(stderr).not.toContain("backdrop-filter shader unavailable");
    expect(stderr).not.toContain("backdrop-filter pipeline creation failed");
  });
});
