/**
 * End-to-end tests for the *browser globals* the `gui` extension exposes.
 *
 * A page written for a real browser reaches the host through bare identifiers
 * (`window`, `document`, `navigator`, `localStorage`, `AudioContext`, …) and
 * drives itself from `requestAnimationFrame` — it never calls `run()`. These
 * tests compile that style of program unchanged and check the pieces the
 * extension promises: the implicit window plus its adoption by
 * `createWindow`, pending DOM handles that bind once `loadHTML` parses the
 * page, host constructors, `localStorage`, `matchMedia`, Annex-B `escape` and
 * the frame pump that keeps an animation running without `run()`.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>ropes page</title></head>
<body><div id="box">hi</div></body></html>`;

describe.skipIf(!guiAvailable)("gui extension — browser globals", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("serves window/document/navigator, host constructors and the frame pump", () => {
    const { value } = harness.compileAndRun(
      "browser",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      // Module-level code runs before the page is loaded, so the handles it
      // captures are *pending* and bind when \`loadHTML\` parses the document.
      const early = document.getElementById("box");
      console.log("pending=" + (early != null));

      // Host constructors work before any window is opened by the program.
      const audio = new AudioContext();
      const gain = audio.createGain();
      gain.gain.value = 0.5;
      console.log("audio=" + audio.sampleRate + ":" + audio.state + ":" + gain.gain.value);
      const buffer = audio.createBuffer(1, 8, 8000);
      const samples = buffer.getChannelData(0);
      samples[3] = 0.25;
      console.log("buffer=" + buffer.length + ":" + samples[3] + ":" + buffer.numberOfChannels);
      // A renderer-only type answers \`undefined\`, so feature probes fall back.
      console.log("webgl=" + (typeof WebGLRenderingContext));

      const win = createWindow({ title: "page", width: 320, height: 240 });
      console.log("adopt=" + (window === win) + ":" + (document.defaultView === win));

      let frames = 0;
      win.on("ready", () => {
        // The pending handle resolved to the real element.
        console.log("bound=" + early.textContent + ":" + document.getElementById("box").textContent);
        console.log("title=" + document.title + ":" + document.readyState + ":" + document.hidden);
        console.log("ua=" + navigator.userAgent.endsWith("xbintsc") + ":" + (navigator.hardwareConcurrency > 0));
        localStorage.setItem("k", "v");
        console.log("storage=" + localStorage.getItem("k") + ":" + localStorage.length);
        console.log("media=" + matchMedia("(pointer: coarse)").matches);
        // Three.js decodes UTF-8 through \`decodeURIComponent(escape(s))\`.
        console.log("escape=" + escape("é"));
        console.log("viewport=" + (window.innerWidth === 320) + ":" + (window.innerHeight === 240));
        // Without \`run()\` the frame pump has to serve each frame itself.
        const step = () => {
          frames += 1;
          if (frames < 3) {
            window.requestAnimationFrame(step);
            return;
          }
          console.log("frames=" + frames);
          win.close();
        };
        window.requestAnimationFrame(step);
      });
      win.loadHTML(page);
      console.log("loaded");
      `,
      { files: { "page.html": PAGE }, autocloseMs: 4000 },
    );

    expect(value("pending")).toBe("true");
    expect(value("audio")).toBe("48000:running:0.5");
    expect(value("buffer")).toBe("8:0.25:1");
    expect(value("webgl")).toBe("undefined");
    expect(value("adopt")).toBe("true:true");
    expect(value("bound")).toBe("hi:hi");
    expect(value("title")).toBe("ropes page:complete:false");
    expect(value("ua")).toBe("true:true");
    expect(value("storage")).toBe("v:1");
    expect(value("media")).toBe("false");
    expect(value("escape")).toBe("%E9");
    expect(value("viewport")).toBe("true:true");
    expect(value("frames")).toBe("3");
  });

  it("treats an html page's plain-JavaScript <script src> as JavaScript", () => {
    /* The `.html` loader wraps a `<script src>` body in a generated module.
     * That body comes from the script file, not from the page, so a `.js`
     * script must stay untyped — otherwise installing a method on a state
     * object (`state.update = …`, how every plain-JS game does it) is reported
     * as `TS4003` against the object literal it was initialized with. */
    const { value } = harness.compileAndRun(
      "html-js-script",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "page", width: 320, height: 240 });
      win.loadHTML(page);
      console.log("loaded");
      `,
      {
        files: {
          "page.html":
            '<html><body><div id="box">hi</div><script type="module" src="./app.js"></script></body></html>',
          "app.js":
            'const state = { phase: "idle" };\n' +
            "state.update = function (n) { this.phase = n; };\n" +
            "state.update(3);\n" +
            'const box = document.getElementById("box");\n' +
            'box.textContent = "js:" + state.phase;\n' +
            'console.log("script=" + box.textContent);\n',
        },
        autocloseMs: 3000,
      },
    );

    expect(value("script")).toBe("js:3");
  });

  it("exits cleanly with a live window that still owns a drawn canvas", () => {
    /* The canvas registry is process-wide and the windows that own the
     * canvases are globals, so `~Canvas2D` runs *after* a function-local static
     * registry would have been destroyed — it removes itself on the way out and
     * crashed on teardown. Drawing, then letting the program end without
     * `close()`, is the sequence that caught it. */
    const { value } = harness.compileAndRun(
      "canvas-teardown",
      `
      import { createWindow } from "gui";

      const win = createWindow({ title: "page", width: 320, height: 240 });
      win.loadHTML('<canvas id="c" width="64" height="64"></canvas>');
      const ctx: any = (document.getElementById("c") as any).getContext("2d");
      ctx.fillStyle = "#ff0000";
      ctx.fillRect(1, 1, 10, 10);
      const gradient: any = ctx.createLinearGradient(0, 0, 64, 0);
      gradient.addColorStop(0, "#ffffff");
      ctx.fillStyle = gradient;
      ctx.fillRect(20, 20, 20, 20);
      console.log("drawn=" + (ctx.fillStyle !== undefined));
      `,
      { autocloseMs: 1200 },
    );

    expect(value("drawn")).toBe("true");
  });

  it("runs a page that never calls createWindow and exits when it stops animating", () => {
    const run = harness.compileAndRun(
      "implicit",
      `
      // No \`import "gui"\` at all: the page only uses browser globals, so the
      // window is opened on first access and the pump stops when the last
      // animation frame is done.
      console.log("size=" + (window.innerWidth === 800) + ":" + (window.innerHeight === 600));
      console.log("doc=" + (document !== undefined) + ":" + (document.defaultView === window));
      let frames = 0;
      const step = () => {
        frames += 1;
        if (frames < 5) {
          requestAnimationFrame(step);
          return;
        }
        console.log("frames=" + frames);
      };
      requestAnimationFrame(step);
      console.log("started");
      `,
      { autocloseMs: 4000 },
    );

    const { value } = run;
    expect(value("size")).toBe("true:true");
    expect(value("doc")).toBe("true:true");
    expect(run.stdout).toContain("started");
    expect(value("frames")).toBe("5");
  });
});
