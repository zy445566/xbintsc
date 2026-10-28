/**
 * End-to-end tests for the `gui` extension — CSS decorative effects:
 * `@media`, generated content, background layers, `text-shadow`, `clip-path`
 * and `filter`.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — effects", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("applies @media rules against the viewport and skips @media print", () => {
    const { value } = harness.compileAndRun(
      "media",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #a { width: 10px; height: 10px; background: #111111; }
        @media (min-width: 600px) { #a { background: #ff0000; } }
        @media (max-width: 500px) { #a { background: #00ff00; } }
        @media (orientation: landscape) { #b { color: #0000ff; } }
        @media print { #a { background: #000000; } }
        @media (min-width: 1000px) { #a { background: #ffff00; } }
        @media screen and (min-width: 100px) and (max-width: 900px) {
          #c { color: #00ff00; }
        }
      </style></head><body>
        <div id="a"></div><div id="b">b</div><div id="c">c</div>
      </body></html>
      \`;

      const win = createWindow({ title: "media", width: 800, height: 600 });
      win.on("ready", () => {
        console.log("a=" + win.computedStyle("#a", "background-color"));
        console.log("b=" + win.computedStyle("#b", "color"));
        console.log("c=" + win.computedStyle("#c", "color"));
        console.log("mq=" + win.matchMedia("(min-width: 600px)").matches);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    // 800x600 matches min-width:600px but not max-width:500px/1000px/print.
    expect(value("a")).toBe("rgb(255, 0, 0)");
    expect(value("b")).toBe("rgb(0, 0, 255)");
    expect(value("c")).toBe("rgb(0, 255, 0)");
    expect(value("mq")).toBe("true");
  });

  it("generates ::before/::after boxes with content and places them in flow", () => {
    const { value, stdout } = harness.compileAndRun(
      "pseudo",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #card { position: relative; width: 120px; height: 30px; background: #202020; color: #ffffff; }
        #card::before {
          content: "B"; display: inline-block; width: 10px; height: 10px;
          background: #00ff00; color: #ff0000;
        }
        #card::after { content: attr(data-tail); color: #0000ff; }
        #none::before { content: none; }
      </style></head><body>
        <div id="card" data-tail="tail">card</div>
        <div id="none">x</div>
      </body></html>
      \`;

      const win = createWindow({ title: "pseudo", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("count=" + win.paintCount());
        console.log("children=" + win.queryCount("#card > *"));
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
    // The synthetic nodes never appear in DOM traversal.
    expect(value("children")).toBe("0");
    // Generated text and the inline-block pseudo background are painted.
    expect(list).toContain('"B"');
    expect(list).toContain('"tail"');
    expect(list).toContain("#00ff00ff");
    // `content: none` generates no box: no colour of its own.
    expect(list).not.toContain("#ffff00ff");
    // The before box comes before the card's own text, the after box after it.
    const before = list.indexOf('"B"');
    const own = list.indexOf('"card"');
    const after = list.indexOf('"tail"');
    expect(before).toBeGreaterThanOrEqual(0);
    expect(own).toBeGreaterThan(before);
    expect(after).toBeGreaterThan(own);
    // `paintCount` counts rectangles: the card's background and ::before's.
    expect(Number(value("count"))).toBe(2);
  });

  it("paints multi-layer linear/radial backgrounds bottom-to-top", () => {
    const { value, stdout } = harness.compileAndRun(
      "backgrounds",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #g {
          width: 100px; height: 60px;
          background-color: #000000;
          background-image:
            radial-gradient(circle at 50% 50%, #ff0000, #00ff00),
            linear-gradient(180deg, #0000ff, #000000);
        }
      </style></head><body><div id="g"></div></body></html>
      \`;

      const win = createWindow({ title: "backgrounds", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("image=" + win.computedStyle("#g", "background-image"));
        console.log("count=" + win.paintCount());
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
    expect(value("image")).toContain("radial-gradient()");
    expect(value("image")).toContain("linear-gradient()");
    // Both layers are painted: the red radial core and its green outer ring
    // (the linear blue layer is underneath them).
    expect(list).toContain("#ff0000ff");
    expect(list).toContain("#00ff00ff");
    expect(list).toMatch(/#0000[0-9a-f]{2}ff/);
    // Radial bands plus the linear strips.
    expect(Number(value("count"))).toBeGreaterThan(30);
  });

  it("tiles repeating gradients across the whole box", () => {
    const { value, stdout } = harness.compileAndRun(
      "repeating-gradient",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #r {
          width: 100px; height: 40px;
          background: repeating-linear-gradient(90deg,
            #ff0000 0%, #ff0000 10%, #0000ff 10%, #0000ff 20%);
        }
      </style></head><body><div id="r"></div></body></html>
      \`;

      const win = createWindow({ title: "repeating-gradient", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("image=" + win.computedStyle("#r", "background-image"));
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
    // The computed value keeps the repeating function name ...
    expect(value("image")).toBe("repeating-linear-gradient()");
    // ... and the whole 100px box is tiled: a 20% period gives five red and
    // five blue bands, not one ramp that holds the final colour.
    expect((list.match(/#ff0000ff/g) ?? []).length).toBeGreaterThan(4);
    expect((list.match(/#0000ffff/g) ?? []).length).toBeGreaterThan(4);
  });

  it("resolves pixel offsets in repeating gradients", () => {
    const { value, stdout } = harness.compileAndRun(
      "repeating-gradient-px",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #r {
          width: 100px; height: 20px;
          background: repeating-linear-gradient(90deg,
            #123456 0px 10px, #abcdef 10px 20px);
        }
      </style></head><body><div id="r"></div></body></html>
      \`;

      const win = createWindow({ title: "repeating-gradient-px", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("image=" + win.computedStyle("#r", "background-image"));
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
    expect(value("image")).toBe("repeating-linear-gradient()");
    expect((list.match(/#123456ff/g) ?? []).length).toBeGreaterThan(4);
    expect((list.match(/#abcdefff/g) ?? []).length).toBeGreaterThan(4);
  });

  it("tiles repeating radial gradients", () => {
    const { value, stdout } = harness.compileAndRun(
      "repeating-radial-gradient",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #r {
          width: 40px; height: 40px;
          background: repeating-radial-gradient(circle at 50% 50%,
            #ff0000 0%, #ff0000 5%, #0000ff 5%, #0000ff 10%);
        }
      </style></head><body><div id="r"></div></body></html>
      \`;

      const win = createWindow({ title: "repeating-radial-gradient", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("image=" + win.computedStyle("#r", "background-image"));
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
    expect(value("image")).toBe("repeating-radial-gradient()");
    expect(list).toContain("#ff0000ff");
    expect(list).toContain("#0000ffff");
  });

  it("paints text-shadow copies behind the text", () => {
    const { value, stdout } = harness.compileAndRun(
      "text-shadow",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #t { font-size: 20px; color: #ffffff; text-shadow: 2px 3px 0 #ff0000; }
      </style></head><body><div id="t">Hi</div></body></html>
      \`;

      const win = createWindow({ title: "text-shadow", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("shadow=" + win.computedStyle("#t", "text-shadow"));
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
    expect(value("shadow")).toBe("2px 3px rgb(255, 0, 0)");
    expect(list).toContain('text x=2.0 baseline=');
    expect(list).toContain('color=#ff0000ff "Hi" shadow=1');
    // The main run is still painted in the text colour.
    expect(list).toContain('color=#ffffffff "Hi"');
  });

  it("clips boxes and descendants with clip-path", () => {
    const { value, stdout } = harness.compileAndRun(
      "clip-path",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #c {
          position: relative; width: 100px; height: 100px; background: #ff0000;
          clip-path: inset(10px 20px 30px 40px);
        }
        #in { position: absolute; left: 0; top: 0; width: 20px; height: 20px; background: #0000ff; }
        #p { width: 50px; height: 50px; background: #00ff00;
             clip-path: polygon(50% 0, 100% 100%, 0 100%); }
      </style></head><body>
        <div id="c"><div id="in"></div></div>
        <div id="p"></div>
      </body></html>
      \`;

      const win = createWindow({ title: "clip", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("inset=" + win.computedStyle("#c", "clip-path"));
        console.log("polygon=" + win.computedStyle("#p", "clip-path"));
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
    expect(value("inset")).toBe("inset(10px 20px 30px 40px)");
    expect(value("polygon")).toContain("polygon(");
    expect(value("polygon")).toContain("50%");
    expect(value("polygon")).toContain("100%");
    // Both the clipped box and its child carry the resolved path index.
    expect(list).toContain("clip-path=0");
    expect(list).toContain("#ff0000ff");
    expect(list).toContain("#0000ffff");
  });

  it("applies colour filters and approximates blur/drop-shadow", () => {
    const { value, stdout } = harness.compileAndRun(
      "filter",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #grey { width: 100px; height: 50px; background: #ff0000;
                filter: grayscale(1) brightness(1.2); }
        #dim { width: 10px; height: 10px; background: #ffffff; filter: opacity(0.5); }
        #soft { position: absolute; left: 0; top: 100px; width: 20px; height: 20px;
                background: #00ff00; filter: blur(4px); }
        #shadow { position: absolute; left: 200px; top: 0; width: 20px; height: 20px;
                  background: #0000ff; filter: drop-shadow(5px 5px 0 #000000); }
        #glass { position: absolute; left: 250px; top: 0; width: 20px; height: 20px;
                 backdrop-filter: blur(4px) brightness(0.5); }
      </style></head><body>
        <div id="grey"></div><div id="dim"></div><div id="soft"></div><div id="shadow"></div>
        <div id="glass"></div>
      </body></html>
      \`;

      const win = createWindow({ title: "filter", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("filter=" + win.computedStyle("#grey", "filter"));
        console.log("backdrop=" + win.computedStyle("#glass", "backdrop-filter"));
        console.log("count=" + win.paintCount());
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
    expect(value("filter")).toBe("grayscale(1) brightness(1.2)");
    // `backdrop-filter` parses/cascades; it is not painted (no backdrop pass).
    expect(value("backdrop")).toBe("blur(4px) brightness(0.5)");
    // grayscale(red) = 0.2126, brightness 1.2 = 0.2551 -> #414141.
    expect(list).toContain("#414141ff");
    // opacity(0.5) halves the painted alpha.
    expect(list).toContain("#ffffff80");
    // blur(4px) spreads the green box into nine translucent copies.
    expect(Number(value("count"))).toBeGreaterThan(9);
    // drop-shadow paints a black rectangle behind the blue box.
    expect(list).toContain("#000000ff");
    expect(list).toContain("#0000ffff");
  });
});
