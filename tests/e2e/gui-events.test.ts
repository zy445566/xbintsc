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

  it("exposes the Web window spellings (addEventListener/removeEventListener/closed)", () => {
    const { value, stdout } = harness.compileAndRun(
      "web-window",
      [
        'import { createWindow, run } from "gui";',
        "",
        'const win = createWindow({ title: "web", width: 200, height: 150 });',
        "const log = [];",
        'const onClick = (e) => log.push("click:" + e.target);',
        'const onKey = (e) => log.push("key:" + e.key);',
        'win.addEventListener("click", onClick);',
        'win.addEventListener("keydown", onKey);',
        'win.removeEventListener("click", onClick);',
        "",
        'win.addEventListener("close", () => console.log("closed-in-handler=" + win.closed));',
        'win.addEventListener("ready", () => {',
        '  console.log("closed-before=" + win.closed);',
        '  win.sendEvent("click", { x: 1, y: 1, button: 0 });',
        '  win.sendEvent("keydown", { key: "a", code: "KeyA" });',
        '  for (const line of log) console.log("web-event=" + line);',
        "  win.close();",
        "});",
        'win.loadHTML("<div id=\'box\'>x</div>");',
        "run();",
        'console.log("closed-after=" + win.closed);',
      ].join("\n"),
    );
    // `closed` is the Web spelling of `isOpen()`.
    expect(value("closed-before")).toBe("false");
    expect(value("closed-after")).toBe("true");
    expect(stdout).toContain("closed-in-handler=true");
    // `removeEventListener` dropped the click listener; the key listener stayed.
    expect(stdout).not.toContain("web-event=click:");
    expect(stdout).toContain("web-event=key:a");
  });

  it("honours listener options, event init fields and dispatch identity", () => {
    const { value } = harness.compileAndRun(
      "event-options",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><body style="margin:0">
        <div id="outer"><div id="inner">x</div></div>
      </body></html>\`;

      const win = createWindow({ title: "event-options", width: 300, height: 200 });
      win.on("ready", () => {
        const doc = win.document;
        const outer: any = doc.getElementById("outer");
        const inner: any = doc.getElementById("inner");

        // once
        let once = 0;
        inner.addEventListener("click", () => { once++; }, { once: true });
        inner.click();
        inner.click();
        console.log("once=" + once);

        // passive: preventDefault is ignored, a normal listener still cancels
        let passive = false;
        inner.addEventListener("click", (e: any) => {
          e.preventDefault();
          passive = e.defaultPrevented;
        }, { passive: true });
        inner.click();
        console.log("passive=" + passive);
        let active = false;
        inner.addEventListener("click", (e: any) => {
          e.preventDefault();
          active = e.defaultPrevented;
        });
        inner.click();
        console.log("active=" + active);

        // signal removes the listener, including an already-aborted one
        const controller = new AbortController();
        let signaled = 0;
        inner.addEventListener("click", () => { signaled++; }, { signal: controller.signal });
        inner.click();
        controller.abort();
        inner.click();
        console.log("signal=" + signaled + ":" + controller.signal.aborted);
        const abortEvent = new AbortController();
        let onAbort = 0;
        abortEvent.signal.addEventListener("abort", () => { onAbort++; });
        abortEvent.abort();
        console.log("abort-event=" + onAbort + ":" + abortEvent.signal.aborted +
                    ":" + (abortEvent.signal.reason instanceof Error));
        const pre = new AbortController();
        pre.abort();
        let never = 0;
        inner.addEventListener("click", () => { never++; }, { signal: pre.signal });
        inner.click();
        console.log("pre-aborted=" + never);

        // window listeners take the same options
        let winOnce = 0;
        win.addEventListener("custom", () => { winOnce++; }, { once: true });
        win.sendEvent("custom", {});
        win.sendEvent("custom", {});
        console.log("win-once=" + winOnce);
        const winAbort = new AbortController();
        let winSignaled = 0;
        win.addEventListener("custom2", () => { winSignaled++; }, { signal: winAbort.signal });
        win.sendEvent("custom2", {});
        winAbort.abort();
        win.sendEvent("custom2", {});
        console.log("win-signal=" + winSignaled);

        // EventInit fields survive construction
        const custom: any = new CustomEvent("ping", { detail: 42, bubbles: true, composed: true });
        console.log("event=" + custom.type + ":" + custom.detail + ":" + custom.bubbles +
                    ":" + custom.composed + ":" + custom.isTrusted + ":" + custom.eventPhase);
        const mouse: any = new MouseEvent("click", { clientX: 7, clientY: 8, button: 2, bubbles: true });
        console.log("mouse=" + mouse.clientX + "," + mouse.clientY + "," + mouse.button);

        // dispatchEvent keeps the event identity and exposes the path/phases
        const phases: string[] = [];
        outer.addEventListener("click", (e: any) => {
          phases.push("capture" + e.eventPhase + ":" + (e.composedPath()[0] === inner));
        }, true);
        inner.addEventListener("click", (e: any) => {
          phases.push("target" + e.eventPhase + ":" + (e.composedPath().length > 1));
        });
        const dispatched: any = new CustomEvent("click", { bubbles: true });
        let seen = false;
        inner.addEventListener("click", (e: any) => { seen = e === dispatched; });
        inner.dispatchEvent(dispatched);
        console.log("path=" + phases.join("|") + ":identity=" + seen + ":target=" + (dispatched.target === inner));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("once")).toBe("1");
    expect(value("passive")).toBe("false");
    expect(value("active")).toBe("true");
    expect(value("signal")).toBe("1:true");
    expect(value("abort-event")).toBe("1:true:true");
    expect(value("pre-aborted")).toBe("0");
    expect(value("win-once")).toBe("1");
    expect(value("win-signal")).toBe("1");
    expect(value("event")).toBe("ping:42:true:true:false:0");
    expect(value("mouse")).toBe("7,8,2");
    // Capture runs ancestor-first (phase 1), the target runs at phase 2, the
    // dispatched object keeps its identity and gains its target.
    expect(value("path")).toBe("capture1:true|target2:true:identity=true:target=true");
  });
});
