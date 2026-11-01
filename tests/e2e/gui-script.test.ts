/**
 * End-to-end tests for the `gui` extension — compiling and running `<script>`
 * bodies (inline and external, with imports) from loaded HTML.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";
import { nodeExtension } from "../../src/extensions/node/index.js";

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
        files: {
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
        files: {
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
      },
    );
    /* 0 -> 1 -> 3 proves the hoisted import (double) is linked. */
    expect(value("srcAfter")).toBe("3");
  });

  it("runs an inline <script> with imports, node:fs included", () => {
    /* An inline body is compiled like a `<script src>` file: its top-level
     * imports are hoisted, a bare one links (`node:fs` needs the node
     * extension) and a relative one is deferred until the script runs. */
    const { value } = harness.compileAndRun(
      "script-inline-imports",
      `
      import { createWindow, run } from "gui";
      import page from "./page-inline.html";

      const win = createWindow({ title: "inline-imports", width: 320, height: 240 });
      win.on("ready", () => {
        const button = win.document.getElementById("b");
        button.click();
        button.click();
        console.log("inlineAfter=" + win.document.getElementById("container").textContent);
        win.close();
      });
      win.loadHTML(page);
      run();
      `,
      {
        extensions: [nodeExtension],
        console: true,
        cwd: harness.workdir,
        files: {
          "page-inline.html": `<html><body>
  <div id="container">0</div>
  <button id="b">0</button>
  <script lang="ts">
    import { readFileSync } from "node:fs";
    import { double } from "./helper";
    const button = document.getElementById("b");
    const label = document.getElementById("container");
    let n = Number(readFileSync("seed.txt", "utf8"));
    button.addEventListener("click", () => {
      n = double(n) + 1;
      button.textContent = String(n);
      label.textContent = String(n);
    });
  </script>
</body></html>`,
          "seed.txt": "2",
          "helper.ts": "export function double(n: number) { return n * 2; }",
        },
      },
    );
    /* 2 -> 5 -> 11: the file read supplied the seed and the deferred helper
     * module was evaluated before the listener ran. */
    expect(value("inlineAfter")).toBe("11");
  });
});
