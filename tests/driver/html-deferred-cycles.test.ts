/**
 * A `gui` `.html` page whose `<script src>` modules form a cycle, re-export a
 * namespace and call a hoisted function that reads a top-level binding.
 *
 * The GUI e2e suite checks the same page end to end, but it is skipped on
 * platforms without a built GUI archive (Windows by default). Compiling to IR
 * exercises the part that broke — the `.html` loader's marker imports, the
 * deferred wrappers and the declaration split — without linking the engine.
 */

import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { compileEntry } from "../../src/driver/frontend.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { guiExtension } from "../../src/extensions/gui/index.js";
import { nodeExtension } from "../../src/extensions/node/index.js";

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-html-deferred-"));
  directories.push(directory);
  return directory;
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

function writeFiles(files: Record<string, string>): string {
  const directory = temporaryDirectory();
  for (const [name, content] of Object.entries(files)) {
    const path = join(directory, name);
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, content);
  }
  return directory;
}

describe("html script deferral", () => {
  it("compiles a page whose deferred scripts form a cycle and re-export a namespace", () => {
    const directory = writeFiles({
      "page.html": `<html><body>
  <script src="./app.ts"></script>
  <script src="./app2.ts"></script>
  <script src="./app3.ts"></script>
</body></html>`,
      "app.ts": 'import { fromCycle } from "./cycle-a";\nconsole.log("app=" + fromCycle());',
      "app2.ts": 'import { Inner } from "./barrel";\nconsole.log("app2=" + Inner.value);',
      "app3.ts": 'import { addBase } from "./const-lib";\nconsole.log("app3=" + addBase(5));',
      "cycle-a.ts": [
        'import { cycleBFn } from "./cycle-b";',
        'export function cycleAFn(): string { return "A"; }',
        "export function fromCycle(): string { return cycleBFn(); }",
      ].join("\n"),
      "cycle-b.ts": [
        'import { cycleAFn } from "./cycle-a";',
        'export function cycleBFn(): string { return "B+" + cycleAFn(); }',
      ].join("\n"),
      "barrel.ts": 'export * as Inner from "./inner";',
      "inner.ts": "export const value = 42;",
      "const-lib.ts": [
        "export const base = 10;",
        "export function addBase(n: number): number { return n + base; }",
      ].join("\n"),
      "main.ts": 'import page from "./page.html";\nconsole.log(page.length > 0);',
    });
    const registry = createDefaultRegistry().register(nodeExtension).register(guiExtension);
    const result = compileEntry(join(directory, "main.ts"), registry, { check: true });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);
    /* The scripts call each import through its wrapper, memoised by the core
     * runtime's `xt_defer_module` — the same implementation the `import()`
     * trigger uses. */
    expect(result.ir).toMatch(/@xt_fn_\d+_xt_deferred_m\d+/);
    expect(result.ir).toContain("@xt_defer_module");
  });
});
