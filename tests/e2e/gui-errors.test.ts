/**
 * End-to-end tests for GUI page-level error reporting.
 *
 * A throw from an engine-invoked callback used to reach `xt_throw` with no
 * `try` frame, print to stderr (invisible in a Windows-subsystem executable)
 * and exit(1): the window simply vanished. The engine now catches callback
 * throws at its own boundary and mirrors the browser surfaces
 * (`window.onerror`, the `error` event, `unhandledrejection`) while keeping the
 * page alive.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — page error reporting", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("reports a throwing click listener and keeps the page alive", () => {
    const result = harness.compileAndRun(
      "click-throw",
      `
      import { createWindow, run } from "gui";

      const win: any = createWindow({ title: "errors", width: 320, height: 200 });
      win.on("ready", () => {
        const doc: any = win.document;
        win.loadHTML("<button id='go'>go</button>");
        console.log("onerror-before=" + typeof win.onerror);
        let seen = "";
        win.onerror = (message: string, source: string, line: number, column: number, error: any) => {
          seen += "onerror:" + (error != null) + ":" + (String(message) === "boom" ? "msg" : "bad");
        };
        win.addEventListener("error", (event: any) => {
          seen += "|event:" + (event.error != null) + ":" + (String(event.message) === "boom" ? "msg" : "bad");
        });
        const button: any = doc.getElementById("go");
        button.addEventListener("click", () => { throw new Error("boom"); });
        button.click();
        // The bridge threw, but the ready callback and the frame loop continue.
        console.log("after-click=alive");
        setTimeout(() => {
          console.log("seen=" + seen);
          win.close();
        }, 30);
      });
      win.loadHTML("<div>start</div>");
      run();
      `,
      { console: true, autocloseMs: 2000 },
    );

    expect(result.value("onerror-before")).toBe("object");
    expect(result.value("after-click")).toBe("alive");
    expect(result.value("seen")).toBe("onerror:true:msg|event:true:msg");
  });

  it("reports an unhandled promise rejection to the window", () => {
    const result = harness.compileAndRun(
      "rejection",
      `
      import { createWindow, run } from "gui";

      const win: any = createWindow({ title: "rejections", width: 320, height: 200 });
      win.on("ready", () => {
        win.onunhandledrejection = (event: any) => {
          console.log("rejection=" + (event.reason.message.indexOf("rejected") >= 0));
          win.close();
        };
        (async () => { throw new Error("rejected"); })();
      });
      win.loadHTML("<div>x</div>");
      run();
      `,
      { console: true, autocloseMs: 2000 },
    );

    expect(result.value("rejection")).toBe("true");
  });
});
