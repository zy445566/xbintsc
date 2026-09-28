/**
 * End-to-end tests for the `gui` extension — hit testing and input event
 * delivery (pointer, wheel, keyboard).
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — events", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("hit-tests elements and delivers input events to handlers", () => {
    const { value, stdout } = harness.compileAndRun(
      "input",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box { width: 100px; height: 50px; }
        #inner { width: 20px; height: 20px; }
      </style></head><body>
        <div id="box"><div id="inner">x</div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "input", width: 400, height: 300 });
      const log = [];
      win.on("mousemove", (e) => log.push("move:" + e.target));
      win.on("mousedown", (e) => log.push("down:" + e.button + ":" + e.target));
      win.on("click", (e) => log.push("click:" + e.x + "," + e.y + ":" + e.target));
      win.on("wheel", (e) => log.push("wheel:" + e.deltaX + "," + e.deltaY));
      win.on("keydown", (e) => log.push("key:" + e.key + ":" + e.shift));

      win.on("ready", () => {
        console.log("hit-inner=" + win.hitTest(5, 5));
        console.log("hit-box=" + win.hitTest(50, 40));
        console.log("hit-none=[" + win.hitTest(390, 290) + "]");
        win.sendEvent("mousemove", { x: 5, y: 5 });
        win.sendEvent("mousedown", { x: 5, y: 5, button: 0 });
        win.sendEvent("click", { x: 5, y: 5, button: 0 });
        win.sendEvent("wheel", { x: 5, y: 5, deltaX: 1, deltaY: 2 });
        win.sendEvent("keydown", { key: "a", code: "KeyA" });
        for (const line of log) console.log("event=" + line);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // Hit testing returns the deepest element under the point.
    expect(value("hit-inner")).toBe("div#inner");
    expect(value("hit-box")).toBe("div#box");
    expect(value("hit-none")).toBe("[]");
    // Handlers receive the payload (coordinates, button, target, deltas, key).
    expect(stdout).toContain("event=move:div#inner");
    expect(stdout).toContain("event=down:0:div#inner");
    expect(stdout).toContain("event=click:5,5:div#inner");
    expect(stdout).toContain("event=wheel:1,2");
    expect(stdout).toContain("event=key:a:false");
  });
});
