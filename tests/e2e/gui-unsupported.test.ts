/**
 * End-to-end tests for the `gui` extension — the warnings that make an
 * unsupported HTML element or CSS property visible instead of silently
 * dropping it.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html>
  <head>
    <style>
      body { color: #111111; }
      .one { unsupported-one: 1px; color: #222222; }
      .two { unsupported-one: 2px; display: flex; }
      @supports (display: grid) { .supports { color: red; } }
      @layer base { .layer { color: blue; } }
      @import url("https://example.com/x.css");
      @font-face { font-family: "Unused"; font-display: swap; }
      .has:has(.one) { color: green; }
      .selection::selection { color: green; }
      .bad[data-x="y" i] { color: red; }
      @media (min-resolution: 2dppx) { .media { color: green; } }
      .features {
        color: hsl(0 100% 50%);
        width: 10vmin;
        transform: perspective(10px);
        filter: url(#somefilter);
        clip-path: path("M0 0 L10 10 Z");
        background-image: conic-gradient(red, blue);
      }
    </style>
  </head>
  <body>
    <table><tr><td>cell</td></tr></table>
    <input value="x">
    <iframe src="about:blank"></iframe>
    <svg width="20" height="20"><text x="0" y="10">x</text></svg>
    <div class="one">one</div>
    <div class="two">two</div>
    <div class="features has">features</div>
    <div class="layer">layer</div>
    <div class="media">media</div>
    <a href="#top">top</a>
    <div srcset="a.png 1x" sizes="100px" controls contenteditable="true" hidden tabindex="0">attrs</div>
  </body>
</html>
`;

const PROGRAM = `
import { createWindow, run } from "gui";

const HTML = ${JSON.stringify(PAGE)};

const win = createWindow({ title: "unsupported", width: 320, height: 240 });
win.on("ready", () => {
  /* A run-time created element is reported too, not just markup from the page. */
  win.document.createElement("select");
  win.document.querySelector(".one").style.setProperty("unsupported-two", "3px");
  win.computedStyle(".one", "color");
  console.log("done=1");
});
win.loadHTML(HTML);
run();
`;

function occurrences(text: string, needle: string): number {
  return text.split(needle).length - 1;
}

describe.skipIf(!guiAvailable)("gui extension — unsupported features", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("reports each unsupported element and CSS property once", () => {
    const { value, stderr } = harness.compileAndRun("unsupported", PROGRAM, {
      console: true,
    });
    expect(value("done")).toBe("1");

    expect(stderr).toContain("unsupported element <table>");
    expect(stderr).toContain("unsupported element <input>");
    expect(stderr).toContain("unsupported element <iframe>");
    /* Created through `document.createElement`, after the page was parsed. */
    expect(stderr).toContain("unsupported element <select>");
    /* Declared by two rules, but reported as one property name. */
    expect(stderr).toContain("unsupported CSS property 'unsupported-one'");
    /* Set through the CSSOM after the page was parsed. */
    expect(stderr).toContain("unsupported CSS property 'unsupported-two'");

    /* At-rules, selectors, media queries and value functions. */
    expect(stderr).toContain("unsupported at-rule '@supports'");
    expect(stderr).toContain("unsupported at-rule '@layer'");
    expect(stderr).toContain("unsupported at-rule '@import'");
    expect(stderr).toContain("unsupported @font-face descriptor 'font-display'");
    expect(stderr).toContain("unsupported pseudo-class ':has'");
    expect(stderr).toContain("unsupported pseudo-element '::selection'");
    expect(stderr).toContain(`unsupported selector '.bad[data-x="y" i]'`);
    expect(stderr).toContain("unsupported media feature 'min-resolution'");
    expect(stderr).toContain("unsupported function 'hsl()' in 'color'");
    expect(stderr).toContain("unsupported CSS unit 'vmin'");
    expect(stderr).toContain("unsupported function 'perspective()' in 'transform'");
    expect(stderr).toContain("unsupported function 'url()' in 'filter'");
    expect(stderr).toContain("unsupported function 'path()' in 'clip-path'");
    expect(stderr).toContain("unsupported function 'conic-gradient()' in 'background-image'");

    /* SVG children and HTML attributes the engine has no behaviour for. */
    expect(stderr).toContain("unsupported SVG element <text>");
    expect(stderr).toContain("unsupported attribute 'srcset' on <div>");
    expect(stderr).toContain("unsupported attribute 'controls' on <div>");
    expect(stderr).toContain("unsupported attribute 'href' on <a>");

    expect(occurrences(stderr, "unsupported CSS property 'unsupported-one'")).toBe(1);
    expect(occurrences(stderr, "unsupported element <table>")).toBe(1);
    expect(occurrences(stderr, "unsupported element <td>")).toBe(1);
    expect(occurrences(stderr, "unsupported attribute 'srcset' on <div>")).toBe(1);

    /* A property the engine implements is never reported. */
    expect(stderr).not.toContain("unsupported CSS property 'color'");
    expect(stderr).not.toContain("unsupported CSS property 'display'");
  });

  it("can silence the reports with XT_GUI_NO_WARNINGS", () => {
    const { value, stderr } = harness.compileAndRun("unsupported-quiet", PROGRAM, {
      console: true,
      env: { XT_GUI_NO_WARNINGS: "1" },
    });
    expect(value("done")).toBe("1");
    expect(stderr).not.toContain("unsupported element");
    expect(stderr).not.toContain("unsupported CSS property");
  });
});
