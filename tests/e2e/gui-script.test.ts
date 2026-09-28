/**
 * End-to-end tests for the `gui` extension — compiling and running `<script>`
 * bodies (inline and external, with imports) from loaded HTML.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — scripts", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("compiles inline <script> bodies from an imported .html asset", () => {
    const { value } = harness.compileAndRun(
      "script",
      `
      import { createWindow, run } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "script", width: 320, height: 240 });
      win.on("ready", () => {
        const button = win.document.getElementById("b");
        button.click();
        button.click();
        console.log("after=" + win.document.getElementById("count").textContent);
        console.log("button=" + button.textContent);
        win.close();
      });
      win.loadHTML(page);
      run();
      `,
      {
        "page.html": `<html><body>
  <div id="count">0</div>
  <button id="b">0</button>
  <script lang="ts">
    const button = document.getElementById("b");
    const label = document.getElementById("count");
    let n = 0;
    button.addEventListener("click", () => {
      n = n + 1;
      button.textContent = String(n);
      label.textContent = String(n);
    });
  </script>
</body></html>`,
      },
    );
    expect(value("after")).toBe("2");
    expect(value("button")).toBe("2");
  });

  it("runs external <script src> modules with imports at load time", () => {
    const { value } = harness.compileAndRun(
      "scriptsrc",
      `
      import { createWindow, run } from "gui";
      import page from "./page-src.html";

      const win = createWindow({ title: "scriptsrc", width: 320, height: 240 });
      win.on("ready", () => {
        const button = win.document.getElementById("b");
        button.click();
        button.click();
        console.log("srcAfter=" + win.document.getElementById("b").textContent);
        win.close();
      });
      win.loadHTML(page);
      run();
      `,
      {
        "page-src.html": `<html><body><button id="b">0</button><script src="./counter.ts"></script></body></html>`,
        "counter.ts": `
import { double } from "./helper";
const button = document.getElementById("b");
let n = 0;
button.addEventListener("click", () => {
  n = double(n) + 1;
  button.textContent = String(n);
});
`,
        "helper.ts": "export function double(n: number) { return n * 2; }",
      },
    );
    /* 0 -> 1 -> 3 proves the hoisted import (double) is linked. */
    expect(value("srcAfter")).toBe("3");
  });
});
