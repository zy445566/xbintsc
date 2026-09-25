/**
 * End-to-end tests for importing packages from `node_modules`. The bundler
 * resolves a bare specifier up the directory tree, follows the package
 * `exports`/`main` fields and merges the package's ESM sources into the entry
 * module. CommonJS packages (which use `require`) are intentionally rejected.
 *
 * Suites are skipped automatically when no clang-compatible compiler is found.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("node_modules packages", (harness) => {
  const { runProgram, expectSameOutputAsNode } = harness;

  it("bundles a package resolved from node_modules", () => {
    const source = 'import { add, two } from "mathx";\nconsole.log(add(two, 3));';
    const files = {
      "node_modules/mathx/package.json": '{ "name": "mathx", "version": "1.0.0", "type": "module", "main": "index.js" }',
      "node_modules/mathx/index.js":
        "export function add(a, b) { return a + b; }\nexport const two = 2;",
    };
    expect(runProgram(source, { files })).toBe("5");
  });

  it("matches Node for a package with a default and named exports", () => {
    const source = `
      import make, { add, two } from "mathx";
      console.log(add(two, 3), make());
    `;
    const files = {
      "node_modules/mathx/package.json": '{ "name": "mathx", "type": "module", "main": "index.js" }',
      "node_modules/mathx/index.js":
        "export function add(a, b) { return a + b; }\nexport const two = 2;\nexport default function make() { return 7; }",
    };
    expectSameOutputAsNode(source, { files });
  });

  it("resolves scoped packages through the exports map", () => {
    const source = `
      import { name } from "@scope/tool";
      import { extra } from "@scope/tool/extra";
      console.log(name, extra);
    `;
    const files = {
      "node_modules/@scope/tool/package.json":
        '{ "name": "@scope/tool", "type": "module", "exports": { ".": { "import": "./dist/index.js" }, "./extra": "./dist/extra.js" } }',
      "node_modules/@scope/tool/dist/index.js": 'export const name = "scoped";',
      "node_modules/@scope/tool/dist/extra.js": "export const extra = 42;",
    };
    expect(runProgram(source, { files })).toBe("scoped 42");
  });

  it("resolves a package imported from a nested relative module", () => {
    const source = 'import { loud } from "./src/app";\nconsole.log(loud);';
    const files = {
      "node_modules/upper/package.json": '{ "name": "upper", "type": "module", "main": "index.js" }',
      "node_modules/upper/index.js": 'export const shout = (s) => "[" + s + "]";',
      "src/app.ts": 'import { shout } from "upper";\nexport const loud = shout("hi");',
    };
    expect(runProgram(source, { files })).toBe("[hi]");
  });
});
