/**
 * The *marker* deferral path — the one an HTML page uses.
 *
 * A `gui` `.html` asset turns each relative import of a `<script src>` (or an
 * inline script) into `import { __xt_deferred_module as marker } from "..."`,
 * and the generated script calls `marker()` when the engine runs it after
 * `loadHTML`. The marker is the bundler's public trigger for "evaluate this
 * module and its graph now", so the same code can be exercised without a
 * window (the GUI suites cover the real `<script src>` end to end in CI).
 *
 * Node's `await import()` uses the same wrappers but a different trigger, and
 * `compile-deferred-reexport.test.ts` covers that side.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (deferred marker imports)", (harness) => {
  const { runProgramFull } = harness;

  it("evaluates the module when the marker runs, once, in call order", () => {
    const result = runProgramFull(
      'import { __xt_deferred_module as loadApp } from "./app";\n' +
        'console.log("before");\n' +
        "const first = loadApp();\n" +
        "const second = loadApp();\n" +
        'console.log("after", first === second, first.value);',
      {
        files: {
          "app.ts": 'console.log("side-effect");\nexport const value = "app";',
        },
      },
    );
    expect(result.stderr).toBe("");
    expect(result.stdout.trim().split("\n")).toEqual(["before", "side-effect", "after true app"]);
  });

  it("lets a module cycle call hoisted functions", () => {
    const result = runProgramFull(
      'import { __xt_deferred_module as loadCa } from "./ca";\nconsole.log(loadCa().aValue);',
      {
        files: {
          "ca.ts":
            'import { recordFn } from "./cb";\n' +
            'export function instantiateFn(): string { return "instantiate"; }\n' +
            "export const aValue = recordFn();",
          "cb.ts":
            'import { instantiateFn } from "./ca";\n' +
            'export function recordFn(): string { return instantiateFn() + "-record"; }',
        },
      },
    );
    expect(result.stderr).toBe("");
    expect(result.stdout.trim()).toBe("instantiate-record");
  });

  it("binds `export * as ns from` inside the deferred graph", () => {
    const result = runProgramFull(
      'import { __xt_deferred_module as loadMid } from "./mid";\nconsole.log(loadMid().read());',
      {
        files: {
          "inner.ts": "export const value = 42;",
          "barrel.ts": 'export * as Inner from "./inner";',
          "mid.ts":
            'import { Inner } from "./barrel";\nexport function read(): number { return Inner.value; }',
        },
      },
    );
    expect(result.stderr).toBe("");
    expect(result.stdout.trim()).toBe("42");
  });

  it("resolves a top-level binding from inside a hoisted function", () => {
    const result = runProgramFull(
      'import { __xt_deferred_module as loadLib } from "./lib";\n' +
        "const { addBase } = loadLib();\n" +
        "console.log(addBase(5));",
      {
        files: {
          "lib.ts":
            "export const base = 10;\n" +
            "export function addBase(n: number): number { return n + base; }",
        },
      },
    );
    expect(result.stderr).toBe("");
    expect(result.stdout.trim()).toBe("15");
  });
});
