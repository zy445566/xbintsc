/**
 * End-to-end tests for the `gui` extension — CSS cascade, stateful selectors
 * (`:hover` / `:focus`) and transitions.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — style", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("parses HTML/CSS and cascades computed styles", () => {
    const { value } = harness.compileAndRun(
      "cascade",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html>
        <head>
          <style>
            body { font-size: 16px; color: #111111; }
            div { width: 10%; }
            .box { width: 25%; }
            #main { width: 60%; color: #222222; }
            .box > .inner { padding-top: 3px; }
            .box .inner { padding-top: 5px; }
            li:first-child { color: #555555; }
            li:nth-child(2) { color: #666666; }
            li:not(.skip) { font-weight: bold; }
            [data-role="lead"] { text-align: center; }
            .em { font-size: 2em; }
            .rem { font-size: 2rem; }
            .imp { color: #000000; }
            .imp { color: #ff0000 !important; }
          </style>
        </head>
        <body>
          <div id="main" class="box">
            <div class="inner" style="color: #00ff00">x</div>
            <div class="inner">y</div>
          </div>
          <ul>
            <li>one</li>
            <li>two</li>
            <li class="skip">three</li>
          </ul>
          <p data-role="lead">lead</p>
          <div class="em">em</div>
          <div class="rem">rem</div>
          <div class="imp" style="color: #0000ff">important</div>
        </body>
      </html>
      \`;

      const win = createWindow({ title: "cascade", width: 800, height: 600 });
      win.on("ready", () => {
        const out = (label, value) => console.log(label + "=" + value);
        out("main-width", win.computedStyle("#main", "width"));
        out("inner-padding", win.computedStyle(".inner", "padding-top"));
        out("inner-color", win.computedStyle(".inner", "color"));
        out("body-color", win.computedStyle("body", "color"));
        out("li1-color", win.computedStyle("li:first-child", "color"));
        out("li2-color", win.computedStyle("li:nth-child(2)", "color"));
        out("li2-weight", win.computedStyle("li:nth-child(2)", "font-weight"));
        out("skip-weight", win.computedStyle("li.skip", "font-weight"));
        out("lead-align", win.computedStyle("[data-role=\\"lead\\"]", "text-align"));
        out("em-size", win.computedStyle(".em", "font-size"));
        out("rem-size", win.computedStyle(".rem", "font-size"));
        out("imp-color", win.computedStyle(".imp", "color"));
        out("li-count", win.queryCount("li"));
        out("lead-count", win.queryCount("[data-role=\\"lead\\"]"));
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("main-width")).toBe("60%"); // id beats class and tag
    expect(value("inner-padding")).toBe("5px"); // equal specificity, later rule wins
    expect(value("inner-color")).toBe("rgb(0, 255, 0)"); // inline beats the sheet
    expect(value("body-color")).toBe("rgb(17, 17, 17)");
    expect(value("li1-color")).toBe("rgb(85, 85, 85)"); // :first-child
    expect(value("li2-color")).toBe("rgb(102, 102, 102)"); // :nth-child(2)
    expect(value("li2-weight")).toBe("700"); // :not(.skip)
    expect(value("skip-weight")).toBe("400"); // :not(.skip) excluded it
    expect(value("lead-align")).toBe("center"); // [data-role="lead"]
    expect(value("em-size")).toBe("32px"); // 2em of the 16px parent
    expect(value("rem-size")).toBe("32px"); // 2rem of the 16px root
    expect(value("imp-color")).toBe("rgb(255, 0, 0)"); // !important beats inline
    expect(value("li-count")).toBe("3");
    expect(value("lead-count")).toBe("1");
  });

  it("applies :hover and :focus state to selector matching", () => {
    const { value } = harness.compileAndRun(
      "state",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box { width: 100px; height: 50px; background: #000000; }
        #box:hover { background: #ff0000; }
        #box:focus { background: #00ff00; }
        #box:hover > #inner { color: #0000ff; }
        #inner { width: 20px; height: 20px; color: #888888; }
      </style></head><body>
        <div id="box"><div id="inner">x</div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "state", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("initial-bg=" + win.computedStyle("#box", "background-color"));
        console.log("initial-color=" + win.computedStyle("#inner", "color"));
        win.sendEvent("mousemove", { x: 50, y: 40 });
        console.log("hover-bg=" + win.computedStyle("#box", "background-color"));
        win.sendEvent("mousemove", { x: 5, y: 5 });
        console.log("inner-hover-color=" + win.computedStyle("#inner", "color"));
        win.sendEvent("mousedown", { x: 50, y: 40, button: 0 });
        console.log("focus-bg=" + win.computedStyle("#box", "background-color"));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("initial-bg")).toBe("rgb(0, 0, 0)"); // no :hover rule match
    expect(value("initial-color")).toBe("rgb(136, 136, 136)");
    expect(value("hover-bg")).toBe("rgb(255, 0, 0)"); // hovering #box
    // Hover also applies to ancestors of the hovered element.
    expect(value("inner-hover-color")).toBe("rgb(0, 0, 255)");
    expect(value("focus-bg")).toBe("rgb(0, 255, 0)"); // :focus wins over :hover
  });

  it("parses selector lists that span lines", () => {
    const { value } = harness.compileAndRun(
      "multiline-selectors",
      `
      import { createWindow, run } from "gui";

      const ACCENT = "#ff0000";
      // A template default exercises the parser's template continuation while
      // the page itself keeps its selector lists on separate lines.
      const page = (gap = \`\${4}px\`) => \`
      <html><head><style>
        .one,
        .two,
        .three {
          color: \${ACCENT};
          padding-top: \${gap};
        }
        .wrap
          > .child,
        .other { width: 55%; }
        [data-label='a,b'],
        [data-x=']'] { font-size: 12px; }
      </style></head><body>
        <div class="one" data-label="a,b">1</div>
        <div class="two" data-x="]">2</div>
        <div class="three">3</div>
        <div class="wrap"><div class="child">c</div></div>
        <div class="other">o</div>
      </body></html>
      \`;

      const win = createWindow({ title: "multiline-selectors", width: 400, height: 300 });
      win.on("ready", () => {
        for (const selector of [".one", ".two", ".three"]) {
          console.log(selector + "=" + win.computedStyle(selector, "color"));
        }
        console.log("pad=" + win.computedStyle(".one", "padding-top"));
        console.log("child-width=" + win.computedStyle(".wrap > .child", "width"));
        console.log("other-width=" + win.computedStyle(".other", "width"));
        console.log("quoted=" + win.computedStyle("[data-label='a,b']", "font-size"));
        console.log("bracket=" + win.computedStyle("[data-x=']']", "font-size"));
        console.log("count=" + win.queryCount(".one,\\n.two"));
        win.close();
      });
      win.loadHTML(page());
      run();
      `,
    );

    expect(value(".one")).toBe("rgb(255, 0, 0)");
    expect(value(".two")).toBe("rgb(255, 0, 0)");
    expect(value(".three")).toBe("rgb(255, 0, 0)");
    expect(value("pad")).toBe("4px"); // the template default
    expect(value("child-width")).toBe("55%");
    expect(value("other-width")).toBe("55%");
    expect(value("quoted")).toBe("12px"); // comma inside a quoted value
    expect(value("bracket")).toBe("12px"); // `]` inside a quoted value
    expect(value("count")).toBe("2"); // a selector list in queryCount
  });

  it("interpolates CSS transitions as the clock advances", () => {
    const { value } = harness.compileAndRun(
      "transition",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box {
          width: 100px; height: 50px; background-color: #000000; color: #888888;
          border-radius: 0px;
          transition: background-color 1000ms linear, color 500ms linear, border-radius 1000ms linear;
        }
        #box:hover { background-color: #ffffff; color: #0000ff; border-radius: 10px; }
      </style></head><body>
        <div id="box">hi</div>
      </body></html>
      \`;

      const win = createWindow({ title: "transition", width: 400, height: 300 });
      win.on("ready", () => {
        win.sendEvent("mousemove", { x: 50, y: 25 });
        console.log("start=" + win.computedStyle("#box", "background-color"));
        win.advance(250);
        console.log("q1-bg=" + win.computedStyle("#box", "background-color"));
        console.log("q1-color=" + win.computedStyle("#box", "color"));
        win.advance(250);
        console.log("q2-bg=" + win.computedStyle("#box", "background-color"));
        console.log("q2-color=" + win.computedStyle("#box", "color"));
        console.log("q2-radius=" + win.computedStyle("#box", "border-radius"));
        win.advance(500);
        console.log("end-bg=" + win.computedStyle("#box", "background-color"));
        console.log("end-radius=" + win.computedStyle("#box", "border-radius"));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("start")).toBe("rgb(0, 0, 0)"); // 0% progress at t=0
    expect(value("q1-bg")).toBe("rgb(64, 64, 64)"); // 25% of black->white
    expect(value("q1-color")).toBe("rgb(68, 68, 196)"); // 50% of the 500ms color run
    expect(value("q2-bg")).toBe("rgb(128, 128, 128)"); // halfway
    expect(value("q2-color")).toBe("rgb(0, 0, 255)"); // finished
    expect(value("q2-radius")).toBe("5px");
    expect(value("end-bg")).toBe("rgb(255, 255, 255)"); // finished
    expect(value("end-radius")).toBe("10px");
  });

  it("serializes border side colors, widths and styles in computed style", () => {
    const { value } = harness.compileAndRun(
      "border-computed",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #f1 { border: 2px solid #00f; width: 60px; height: 20px; }
        #f2 { border-top-color: #f00; border-top-width: 3px; border-top-style: dashed; }
        #f3 { border-width: 1px 2px 3px 4px; border-style: dotted; color: #0f0; }
        #f4 { }
      </style></head><body>
        <div id="f1"></div><div id="f2"></div><div id="f3"></div><div id="f4"></div>
      </body></html>\`;

      const win = createWindow({ title: "border", width: 320, height: 200 });
      win.on("ready", () => {
        win.loadHTML(HTML);
        console.log("f1=" + win.computedStyle("#f1", "border-top-color") + ":" +
                    win.computedStyle("#f1", "border-top-width") + ":" +
                    win.computedStyle("#f1", "border-top-style"));
        console.log("f2=" + win.computedStyle("#f2", "border-top-color") + ":" +
                    win.computedStyle("#f2", "border-top-width") + ":" +
                    win.computedStyle("#f2", "border-top-style"));
        console.log("f3=" + win.computedStyle("#f3", "border-left-width") + ":" +
                    win.computedStyle("#f3", "border-bottom-width") + ":" +
                    win.computedStyle("#f3", "border-top-color"));
        console.log("f4=" + win.computedStyle("#f4", "border-top-color") + ":" +
                    win.computedStyle("#f4", "border-top-width") + ":" +
                    win.computedStyle("#f4", "border-top-style"));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("f1")).toBe("rgb(0, 0, 255):2px:solid");
    expect(value("f2")).toBe("rgb(255, 0, 0):3px:dashed");
    // A single color declaration resolves to currentColor and applies to every
    // side; per-side widths stay distinct.
    expect(value("f3")).toBe("4px:3px:rgb(0, 255, 0)");
    // No border: a used width of 0, style `none`, currentColor.
    expect(value("f4")).toBe("rgb(0, 0, 0):0px:none");
  });

  it("matches :focus-within on the focused element and its ancestors", () => {
    const { value } = harness.compileAndRun(
      "focus-within",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #card { background-color: #111111; width: 120px; height: 48px; }
        #card:focus-within { background-color: #00ff00; }
        #deep:focus-within { background-color: #0000ff; }
      </style></head><body>
        <div id="card">
          <input id="inner" value="x">
          <div id="deep"><input id="deep-input"></div>
        </div>
      </body></html>\`;

      const win = createWindow({ title: "focus-within", width: 300, height: 200 });
      win.on("ready", () => {
        const doc: any = win.document;
        win.loadHTML(HTML);
        console.log("before=" + win.computedStyle("#card", "background-color"));
        doc.getElementById("inner").focus();
        console.log("inner=" + win.computedStyle("#card", "background-color"));
        doc.getElementById("deep-input").focus();
        console.log("deep=" + win.computedStyle("#card", "background-color") + ":" +
                    win.computedStyle("#deep", "background-color"));
        doc.getElementById("deep-input").blur();
        console.log("blur=" + win.computedStyle("#card", "background-color"));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("before")).toBe("rgb(17, 17, 17)");
    expect(value("inner")).toBe("rgb(0, 255, 0)");
    expect(value("deep")).toBe("rgb(0, 255, 0):rgb(0, 0, 255)");
    expect(value("blur")).toBe("rgb(17, 17, 17)");
  });
});
