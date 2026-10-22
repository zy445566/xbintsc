/**
 * End-to-end tests for the `gui` extension — inline `<svg>` elements.
 *
 * An inline `<svg>` tree is not an SVG *file*: the document already holds the
 * nodes, so the engine serializes the subtree back into an SVG document and
 * rasterises it exactly like `<img src="logo.svg">`. These tests pin the
 * replaced-element sizing, the display-list entry, the serializer's canonical
 * SVG names (the HTML parser lowercases `viewBox`/`linearGradient`) and the
 * fact that a DOM mutation re-serializes the artwork.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — inline svg", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("sizes and paints an inline svg as a replaced element", () => {
    const { stdout, stderr } = harness.compileAndRun(
      "inline-svg",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #scaled { width: 64px; height: 48px; }
      </style></head><body>
        <svg id="natural" width="8" height="6" viewBox="0 0 8 6">
          <rect width="8" height="6" fill="#ff0000"/>
          <circle cx="4" cy="3" r="2" fill="#0000ff"/>
        </svg>
        <svg id="viewbox" viewBox="0 0 20 10">
          <rect width="20" height="10" fill="#00ff00"/>
        </svg>
        <svg id="scaled" width="8" height="6" viewBox="0 0 8 6">
          <rect width="8" height="6" fill="#ff0000"/>
        </svg>
      </body></html>
      \`;

      const win = createWindow({ title: "inline-svg", width: 400, height: 300 });
      win.on("ready", () => {
        const natural = win.getBoundingClientRect("#natural");
        console.log("natural=" + natural.width + "x" + natural.height);
        const viewbox = win.getBoundingClientRect("#viewbox");
        console.log("viewbox=" + viewbox.width + "x" + viewbox.height);
        const scaled = win.getBoundingClientRect("#scaled");
        console.log("scaled=" + scaled.width + "x" + scaled.height);
        console.log("display=" + win.computedStyle("#natural", "display"));
        console.log("hit=" + win.hitTest(4, 3));
        console.log("no-rects=" + (win.layoutTree().indexOf("<rect>") < 0));
        console.log("paint-list-start");
        console.log(win.paintList());
        console.log("paint-list-end");
        const info = win.inlineSvg("#natural");
        console.log("markup=" + info.markup);
        console.log("parsed=" + info.width + "x" + info.height + " shapes=" + info.shapes);
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    // width/height attributes give the intrinsic size; a bare viewBox is the
    // fallback; an explicit CSS size wins over both.
    expect(stdout).toContain("natural=8x6");
    expect(stdout).toContain("viewbox=20x10");
    expect(stdout).toContain("scaled=64x48");
    // The `<svg>` box itself is the hit-test target; its children are vector
    // art, not HTML boxes, so the walk stays at the svg element.
    expect(stdout).toContain("hit=svg#natural");
    expect(stdout).toContain("display=inline-block");
    // The svg subtree produces no HTML boxes: only the replaced box is laid out.
    expect(stdout).toContain("no-rects=true");
    // A source newline between two inline elements is a hard line break in this
    // engine (like `<br>`), so the three boxes stack in document order.
    const list = stdout.slice(stdout.indexOf("paint-list-start"), stdout.indexOf("paint-list-end"));
    expect(list).toContain("image x=0.0 y=0.0 w=8.0 h=6.0 inline-svg=");
    expect(list).toContain("image x=0.0 y=6.0 w=20.0 h=10.0 inline-svg=");
    expect(list).toContain("image x=0.0 y=16.0 w=64.0 h=48.0 inline-svg=");
    // The serialized document restores the case-sensitive SVG names the HTML
    // parser lowercased, and only the svg subtree is included.
    const markup = stdout
      .split("\n")
      .find((line) => line.startsWith("markup="))!
      .slice("markup=".length);
    expect(markup.startsWith('<svg xmlns="http://www.w3.org/2000/svg" id="natural" width="8" height="6" viewBox="0 0 8 6">')).toBe(true);
    expect(markup).toContain('<rect width="8" height="6" fill="#ff0000"></rect>');
    expect(markup).toContain('<circle cx="4" cy="3" r="2" fill="#0000ff"></circle>');
    expect(markup).not.toContain("viewbox=");
    expect(stdout).toContain("parsed=8x6 shapes=2");
    // Rasterising a parsed document must actually succeed.
    expect(stderr).not.toContain("could not rasterise inline SVG");
  });

  it("parses gradients, re-serializes on mutation and restyles the box", () => {
    const { stdout, stderr } = harness.compileAndRun(
      "inline-svg-mutate",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #icon { background: #123456; border: 2px solid #00ff00; }
      </style></head><body>
        <svg id="icon" width="24" height="24" viewBox="0 0 24 24">
          <defs>
            <linearGradient id="grad" gradientUnits="userSpaceOnUse" x1="0" y1="0" x2="24" y2="24">
              <stop offset="0%" stop-color="#ff0000"/>
              <stop offset="100%" stop-color="#0000ff"/>
            </linearGradient>
          </defs>
          <rect id="box" width="24" height="24" fill="url(#grad)"/>
        </svg>
      </body></html>
      \`;

      const win = createWindow({ title: "inline-svg-mutate", width: 400, height: 300 });
      win.on("ready", () => {
        const first = win.inlineSvg("#icon");
        console.log("shapes=" + first.shapes);
        console.log("gradient=" + (first.markup.indexOf("linearGradient") >= 0));
        console.log("units=" + (first.markup.indexOf('gradientUnits="userSpaceOnUse"') >= 0));
        console.log("stops=" + first.markup.split("<stop").length);

        const icon = win.document.getElementById("icon");
        icon.setAttribute("width", "48");
        const rect = win.document.querySelector("rect");
        rect.setAttribute("fill", "#00ff00");
        const grown = win.getBoundingClientRect("#icon");
        const second = win.inlineSvg("#icon");
        console.log("grown=" + grown.width + "x" + grown.height);
        console.log("moved=" + (second.markup.indexOf('fill="#00ff00"') >= 0));

        icon.innerHTML = '<circle cx="12" cy="12" r="10" fill="#ffff00"/>';
        const third = win.inlineSvg("#icon");
        console.log("replaced=" + (third.markup.indexOf("<circle") >= 0) + " shapes=" + third.shapes);
        console.log("paint-list-start");
        console.log(win.paintList());
        console.log("paint-list-end");
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    expect(stdout).toContain("shapes=1");
    // The HTML parser lowercases `linearGradient`; the serializer restores it,
    // or NanoSVG would silently drop the gradient definition.
    expect(stdout).toContain("gradient=true");
    expect(stdout).toContain("units=true");
    // start + two stops = three occurrences of "<stop".
    expect(stdout).toContain("stops=3");
    // A presentation-attribute mutation changes the intrinsic size, and a paint
    // mutation changes the serialized document the renderer rasterises.
    expect(stdout).toContain("grown=52x28");
    expect(stdout).toContain("moved=true");
    // `innerHTML` swaps the artwork for a new subtree.
    expect(stdout).toContain("replaced=true shapes=1");
    const list = stdout.slice(stdout.indexOf("paint-list-start"), stdout.indexOf("paint-list-end"));
    // The svg element is still an ordinary styled box: background + 4 borders.
    expect(list).toContain("color=#123456ff");
    expect((list.match(/#00ff00ff/g) ?? []).length).toBe(4);
    expect(list).toContain("w=48.0 h=24.0 inline-svg=");
    expect(stderr).not.toContain("could not rasterise inline SVG");
  });
});
