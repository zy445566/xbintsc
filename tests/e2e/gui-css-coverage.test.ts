/**
 * End-to-end coverage for the CSS surface used by the Inside the Ropes
 * stylesheets: typography, inline flex/grid, grid-area, aspect-ratio, auto
 * margins, percentage radii, outlines, vertical text, SVG presentation
 * properties and sized background tiles. Assertions use layout geometry,
 * `computedStyle`, `paintList` and the `inlineSvg` diagnostic, so they are
 * deterministic without reading GPU pixels.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — CSS coverage", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("applies letter-spacing and text-transform to layout and paint", () => {
    const { value, stdout } = harness.compileAndRun(
      "typography",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #a, #b, #c {
          position: absolute; top: 0; font-size: 20px; white-space: nowrap;
        }
        #a { left: 0; }
        #b { left: 200px; letter-spacing: 2px; }
        #c { left: 350px; text-transform: uppercase; }
      </style></head><body>
        <div id="a">abc</div><div id="b">abc</div><div id="c">abc</div>
      </body></html>\`;

      const win = createWindow({ title: "typography", width: 500, height: 120 });
      win.on("ready", () => {
        const a = win.getBoundingClientRect("#a");
        const b = win.getBoundingClientRect("#b");
        console.log("delta=" + (b.width - a.width).toFixed(1));
        console.log("spacing=" + win.computedStyle("#b", "letter-spacing"));
        console.log("transform=" + win.computedStyle("#c", "text-transform"));
        console.log(win.paintList());
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    // Three characters, two pixels each, including the trailing one.
    expect(value("delta")).toBe("6.0");
    expect(value("spacing")).toBe("2px");
    expect(value("transform")).toBe("uppercase");
    expect(stdout).toContain('"ABC"');
  });

  it("resolves currentColor and percentage border radii", () => {
    const { value, stdout } = harness.compileAndRun(
      "colors-radius",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #dot {
          color: #ff0000;
          background: currentColor;
          width: 20px; height: 20px;
          border-radius: 50%;
        }
      </style></head><body><div id="dot"></div></body></html>\`;

      const win = createWindow({ title: "colors", width: 200, height: 120 });
      win.on("ready", () => {
        console.log("bg=" + win.computedStyle("#dot", "background-color"));
        console.log("radius=" + win.computedStyle("#dot", "border-radius"));
        console.log(win.paintList());
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("bg")).toBe("rgb(255, 0, 0)");
    expect(value("radius")).toBe("50%");
    expect(stdout).toContain("r=10.0 color=#ff0000ff");
  });

  it("lays out inline-flex and inline-grid boxes atomically", () => {
    const { value } = harness.compileAndRun(
      "inline-containers",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #row { display: inline-flex; gap: 10px; }
        #row i { display: block; width: 20px; height: 20px; background: #222; }
        #key {
          display: inline-grid; place-items: center;
          width: 17px; height: 17px; vertical-align: -3px;
          background: #333;
        }
      </style></head><body>
        <div id="line"><span id="row"><i></i><i></i></span><span id="key">W</span></div>
      </body></html>\`;

      const win = createWindow({ title: "inline", width: 400, height: 120 });
      win.on("ready", () => {
        const row = win.getBoundingClientRect("#row");
        const key = win.getBoundingClientRect("#key");
        console.log("row=" + row.width.toFixed(1) + "x" + row.height.toFixed(1));
        console.log("key=" + key.width.toFixed(1) + "x" + key.height.toFixed(1));
        console.log("flex=" + win.computedStyle("#row", "display"));
        console.log("grid=" + win.computedStyle("#key", "display"));
        console.log("align=" + win.computedStyle("#key", "vertical-align"));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("row")).toBe("50.0x20.0");
    expect(value("key")).toBe("17.0x17.0");
    expect(value("flex")).toBe("inline-flex");
    expect(value("grid")).toBe("inline-grid");
    expect(value("align")).toBe("-3px");
  });

  it("places grid-area items and derives height from aspect-ratio", () => {
    const { value } = harness.compileAndRun(
      "grid-aspect",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #grid {
          display: grid;
          grid-template-columns: repeat(3, 30px);
          grid-template-rows: repeat(3, 30px);
        }
        #cell { grid-area: 2 / 3; background: #444; }
        #ratio { width: 300px; aspect-ratio: 3 / 4; background: #555; }
      </style></head><body>
        <div id="grid"><div id="cell"></div></div>
        <div id="ratio"></div>
      </body></html>\`;

      const win = createWindow({ title: "grid", width: 500, height: 500 });
      win.on("ready", () => {
        const cell = win.getBoundingClientRect("#cell");
        const ratio = win.getBoundingClientRect("#ratio");
        console.log("cell=" + cell.x.toFixed(1) + "," + cell.y.toFixed(1));
        console.log("ratio=" + ratio.width.toFixed(1) + "x" + ratio.height.toFixed(1));
        console.log("area=" + win.computedStyle("#cell", "grid-area"));
        console.log("ar=" + win.computedStyle("#ratio", "aspect-ratio"));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("cell")).toBe("60.0,30.0");
    expect(value("ratio")).toBe("300.0x400.0");
    expect(value("area")).toBe("2 / 3 / 0 / 0");
    expect(value("ar")).toBe("0.75");
  });

  it("paints focus-visible outlines and applies :active transforms", () => {
    const { value, stdout } = harness.compileAndRun(
      "outline-active",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #btn {
          position: absolute; left: 10px; top: 10px;
          width: 60px; height: 30px;
          background: #111;
          border: 0;
          outline: 2px solid #ff0000;
          outline-offset: 3px;
        }
        #btn:focus-visible { outline-color: #00ff00; }
        #btn:active { transform: translateY(5px); }
      </style></head><body><button id="btn">go</button></body></html>\`;

      const win = createWindow({ title: "outline", width: 200, height: 120 });
      win.on("ready", () => {
        const doc = win.document;
        const btn: any = doc.querySelector("#btn");
        btn.focus();
        console.log("color=" + win.computedStyle("#btn", "outline-color"));
        console.log("width=" + win.computedStyle("#btn", "outline-width"));
        console.log("offset=" + win.computedStyle("#btn", "outline-offset"));
        console.log(win.paintList());
        win.sendEvent("mousedown", { x: 20, y: 20, button: 0 });
        console.log("active=" + win.computedStyle("#btn", "transform"));
        win.sendEvent("mouseup", { x: 20, y: 20, button: 0 });
        console.log("released=" + win.computedStyle("#btn", "transform"));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("color")).toBe("rgb(0, 255, 0)");
    expect(value("width")).toBe("2px");
    expect(value("offset")).toBe("3px");
    expect(stdout).toContain("rect x=7.0 y=7.0 w=66.0 h=2.0");
    expect(value("active")).toContain("translateY(5px)");
    expect(value("released")).toBe("none");
  });

  it("lays vertical writing mode text out in a column and rotates it", () => {
    const { value, stdout } = harness.compileAndRun(
      "vertical",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #v {
          position: absolute; left: 200px; top: 0;
          writing-mode: vertical-rl;
          font-size: 10px;
        }
      </style></head><body><div id="v">ABCD</div></body></html>\`;

      const win = createWindow({ title: "vertical", width: 400, height: 200 });
      win.on("ready", () => {
        const v = win.getBoundingClientRect("#v");
        console.log("rect=" + v.width.toFixed(1) + "," + v.height.toFixed(1));
        console.log("mode=" + win.computedStyle("#v", "writing-mode"));
        console.log(win.paintList());
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    const size = value("rect").split(",");
    const width = Number(size[0] ?? 0);
    const height = Number(size[1] ?? 0);
    expect(width).toBeLessThan(20);
    expect(height).toBeGreaterThan(width);
    expect(value("mode")).toBe("vertical-rl");
    expect(stdout).toContain(",1,-1,");
  });

  it("injects document CSS into the serialized inline SVG", () => {
    const { value } = harness.compileAndRun(
      "svg-css",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        .ring { fill: none; stroke: #ff0000; stroke-width: 2; }
        .glow { fill: #00ff00; stroke: rgba(0, 0, 255, 0.5); }
      </style></head><body>
        <svg id="icon" viewBox="0 0 20 20">
          <circle class="ring" cx="10" cy="10" r="8"></circle>
          <rect class="glow" x="1" y="1" width="4" height="4"></rect>
        </svg>
      </body></html>\`;

      const win = createWindow({ title: "svg", width: 120, height: 120 });
      win.on("ready", () => {
        const info: any = win.inlineSvg("#icon");
        console.log("shapes=" + info.shapes);
        console.log("ring=" + (info.markup.indexOf("style=\\"fill:none;stroke:#ff0000;stroke-width:2\\"") >= 0));
        console.log("glow=" + (info.markup.indexOf("fill:#00ff00") >= 0));
        console.log("opacity=" + (info.markup.indexOf("stroke-opacity:0.500") >= 0));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("shapes")).toBe("2");
    expect(value("ring")).toBe("true");
    expect(value("glow")).toBe("true");
    expect(value("opacity")).toBe("true");
  });

  it("tiles an image background at background-size", () => {
    const image = harness.imageSrc(harness.logoPath);
    const { stdout } = harness.compileAndRun(
      "background-size",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #bg { width: 250px; height: 120px; background-image: url("${image}"); background-size: 100px 100px; }
      </style></head><body><div id="bg"></div></body></html>\`;

      const win = createWindow({ title: "tiles", width: 500, height: 300 });
      win.on("ready", () => {
        console.log(win.paintList());
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    const tiles = stdout.split("\n").filter((line) => line.includes("w=100.0 h=100.0"));
    expect(tiles.length).toBe(6);
    expect(stdout).toContain("image x=0.0 y=0.0 w=100.0 h=100.0");
  });

  it("distributes auto margins, inherits fonts and copies clip-path", () => {
    const { value } = harness.compileAndRun(
      "auto-margins",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; font-size: 30px; }
        button { font: inherit; }
        #center { width: 100px; height: 10px; margin: 0 auto; background: #666; }
        #flex { display: flex; width: 300px; height: 10px; }
        #hint { width: 20px; height: 10px; margin-left: auto; background: #777; }
        #outer {
          width: 100px; height: 100px;
          clip-path: polygon(0 0, 100% 0, 100% 100%, 0 100%);
        }
        #outer::before { content: ""; clip-path: inherit; }
      </style></head><body>
        <div id="center"></div>
        <div id="flex"><div id="hint"></div></div>
        <div id="outer"></div>
        <button id="b">x</button>
      </body></html>\`;

      const win = createWindow({ title: "margins", width: 400, height: 300 });
      win.on("ready", () => {
        const center = win.getBoundingClientRect("#center");
        const hint = win.getBoundingClientRect("#hint");
        console.log("center=" + center.x.toFixed(1));
        console.log("hint=" + hint.x.toFixed(1));
        console.log("font=" + win.computedStyle("#b", "font-size"));
        const outer: any = win.document.querySelector("#outer");
        const inherited: any = getComputedStyle(outer, "::before").getPropertyValue("clip-path");
        const own: any = getComputedStyle(outer).getPropertyValue("clip-path");
        console.log("clip=" + (inherited === own) + ":" + own);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(value("center")).toBe("150.0");
    expect(value("hint")).toBe("280.0");
    expect(value("font")).toBe("30px");
    expect(value("clip")).toContain("true:polygon(");
  });
});
