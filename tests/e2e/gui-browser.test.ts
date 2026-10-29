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
      // fetch is a core runtime global, not a gui extension export.
      console.log("fetch=" + typeof fetch);

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
    expect(value("webgl")).toBe("function");
    expect(value("fetch")).toBe("function");
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

  it("scrolls the page, focuses the window, answers dialogs and reports media changes", () => {
    const { value, stderr } = harness.compileAndRun(
      "navigation",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>body { margin: 0; }</style></head><body style="height:1000px">
        <div id="far" style="position:absolute;top:205px;left:0;width:10px;height:20px"></div>
      </body></html>\`;

      const win = createWindow({ title: "navigation", width: 300, height: 300 });
      win.on("ready", () => {
        const doc = win.document;
        const far: any = doc.getElementById("far");
        console.log("scroll0=" + window.scrollX + ":" + window.scrollY + ":" + window.pageYOffset);
        window.scrollTo(0, 200);
        console.log("scroll1=" + window.scrollY + ":" + win.hitTest(5, 5));
        win.sendEvent("wheel", { x: 5, y: 5, deltaY: 100 });
        console.log("wheel=" + window.scrollY);
        window.scrollTo({ left: 0, top: 50 });
        console.log("scroll2=" + window.scrollX + ":" + window.scrollY);

        window.scrollTo(0, 700);
        far.focus();
        console.log("focus-scroll=" + window.scrollY);
        window.scrollTo(0, 700);
        far.focus({ preventScroll: true });
        console.log("prevent-scroll=" + window.scrollY);
        console.log("has-focus=" + doc.hasFocus());
        win.focus();
        console.log("focused=" + doc.hasFocus());
        win.blur();
        console.log("blurred=" + doc.hasFocus());

        console.log("confirm=" + confirm("Delete?"));
        console.log("prompt=" + prompt("Name?", "abc"));

        const mq: any = matchMedia("(min-width: 400px)");
        let changes = 0;
        mq.addEventListener("change", () => { changes++; });
        console.log("mq0=" + mq.matches);
        win.setSize(500, 300);
        win.setTimeout(() => {
          console.log("mq1=" + mq.matches + ":" + changes);
          win.close();
        }, 150);
      });
      win.loadHTML(HTML);
      run();
      `,
      { autocloseMs: 3000 },
    );

    expect(value("scroll0")).toBe("0:0:0");
    expect(value("scroll1")).toBe("200:div#far");
    expect(value("wheel")).toBe("300");
    expect(value("scroll2")).toBe("0:50");
    expect(value("focus-scroll")).toBe("205");
    expect(value("prevent-scroll")).toBe("700");
    expect(value("has-focus")).toBe("false");
    expect(value("focused")).toBe("true");
    expect(value("blurred")).toBe("false");
    expect(value("confirm")).toBe("false");
    expect(value("prompt")).toBe("abc");
    expect(stderr).toContain("confirm: Delete?");
    expect(stderr).toContain("prompt: Name?");
    expect(value("mq0")).toBe("false");
    expect(value("mq1")).toBe("true:1");
  });
});
