/**
 * `export * as ns from "./dep"` inside a dynamically imported module.
 *
 * Both modules are *deferred*: the dependency's export names only exist inside
 * its wrapper function. The bundler used to rebuild the namespace export as an
 * object literal of final names, which referenced names that are not in scope —
 * TS3002 when the checker saw it, `undefined` at run time otherwise (this is
 * how typebox's `export * as Guard from './guard.mjs'` broke pi). The deferred
 * module now exports the dependency's wrapper result, which is its exports
 * object.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (deferred namespace re-export)", (harness) => {
  const { runProgramFull } = harness;

  it("exports the dependency's namespace through `export * as`", () => {
    const result = runProgramFull('const mod = await import("./mid");\nconsole.log(mod.read());', {
      files: {
        "inner.ts": "export const value = 42;\nexport function double(n: number): number { return n * 2; }",
        "barrel.ts": 'export * as Inner from "./inner";',
        "mid.ts":
          'import { Inner } from "./barrel";\n' +
          "export function read(): number {\n" +
          "  return Inner.value + Inner.double(1);\n" +
          "}",
      },
    });
    expect(result.stderr).toBe("");
    expect(result.stdout.trim()).toBe("44");
  });

  it("keeps a plain namespace import and a cycle working in a deferred module", () => {
    const result = runProgramFull(
      'const mod = await import("./a");\nconsole.log(mod.getB(), mod.fromA);',
      {
        files: {
          "a.ts": 'export const fromA = "a";\nexport function getB(): string { return b.fromB; }\nimport * as b from "./b";',
          "b.ts": 'export const fromB = "b";\nexport function getA(): string { return a.fromA; }\nimport * as a from "./a";',
        },
      },
    );
    expect(result.stderr).toBe("");
    expect(result.stdout.trim()).toBe("b a");
  });
  it("hoists function declarations so a module cycle can call them", () => {
    const result = runProgramFull('const mod = await import("./ca");\nconsole.log(mod.aValue);', {
      files: {
        "ca.ts":
          'import { recordFn } from "./cb";\n' +
          'export function instantiateFn(): string { return "instantiate"; }\n' +
          "export const aValue = recordFn();",
        "cb.ts":
          'import { instantiateFn } from "./ca";\n' +
          'export function recordFn(): string { return instantiateFn() + "-record"; }',
      },
    });
    expect(result.stderr).toBe("");
    expect(result.stdout.trim()).toBe("instantiate-record");
  });
  it("evaluates a dynamically imported module once, at the import", () => {
    const result = runProgramFull(
      'console.log("before");\n' +
        'const first = await import("./app");\n' +
        'const second = await import("./app");\n' +
        'console.log("after", first === second, first.value);',
      { files: { "app.ts": 'console.log("side-effect");\nexport const value = "app";' } },
    );
    expect(result.stderr).toBe("");
    expect(result.stdout.trim().split("\n")).toEqual(["before", "side-effect", "after true app"]);
  });

  it("resolves a top-level binding from inside a hoisted function", () => {
    const result = runProgramFull(
      'const { addBase } = await import("./lib");\nconsole.log(addBase(5));',
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