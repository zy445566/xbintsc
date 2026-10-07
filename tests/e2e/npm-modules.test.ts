/**
 * End-to-end tests for importing packages from `node_modules`. The bundler
 * resolves a bare specifier up the directory tree, follows the package
 * `exports`/`main` fields and merges the package's ESM sources into the entry
 * module. CommonJS packages (which use `require`) are supported when they live
 * under `node_modules`: the bundler gives each one a synthetic `module`/`exports`
 * pair and rewrites `require` calls. `require` in user code (outside
 * `node_modules`) is still rejected by the code generator.
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

  it("imports named exports from a CommonJS package", () => {
    const source = 'import { pad, extra } from "leftpad";\nconsole.log(pad("5", 3), extra);';
    const files = {
      "node_modules/leftpad/package.json": '{ "name": "leftpad", "main": "index.js" }',
      "node_modules/leftpad/index.js":
        'exports.pad = function (s, n) { var out = s; while (out.length < n) out = "0" + out; return out; };\nmodule.exports.extra = 7;',
    };
    expectSameOutputAsNode(source, { files, name: "cjs-named" });
  });

  it("imports the whole `module.exports` object as the default export", () => {
    const source = 'import leftpad from "leftpad";\nconsole.log(leftpad.pad("7", 2), leftpad.extra);';
    const files = {
      "node_modules/leftpad/package.json": '{ "name": "leftpad", "main": "index.js" }',
      "node_modules/leftpad/index.js":
        'module.exports = { pad: function (s, n) { return s + n; }, extra: 3 };',
    };
    expectSameOutputAsNode(source, { files, name: "cjs-default" });
  });

  it("follows `require` between CommonJS packages", () => {
    const source = 'import { run } from "usecjs";\nconsole.log(run());';
    const files = {
      "node_modules/leftpad/package.json": '{ "name": "leftpad", "main": "index.js" }',
      "node_modules/leftpad/index.js":
        'exports.pad = function (s, n) { var out = s; while (out.length < n) out = "0" + out; return out; };',
      "node_modules/usecjs/package.json": '{ "name": "usecjs", "main": "index.js" }',
      "node_modules/usecjs/index.js":
        'var lp = require("leftpad");\nmodule.exports.run = function () { return lp.pad("5", 3); };',
    };
    expectSameOutputAsNode(source, { files, name: "cjs-require" });
  });

  it("requires an ESM module from CommonJS", () => {
    const source = 'import f from "c";\nconsole.log(f());';
    const files = {
      "node_modules/c/package.json": '{ "name": "c", "main": "index.js" }',
      "node_modules/c/index.js":
        'var dep = require("./dep.js");\nmodule.exports = function () { return dep.hello(); };',
      "node_modules/c/dep.js": 'export function hello() { return "from-esm"; }',
    };
    expectSameOutputAsNode(source, { files, name: "cjs-require-esm" });
  });

  it("supports `require` of a built-in from a CommonJS package", () => {
    const source = 'import f from "usepath";\nconsole.log(f());';
    const files = {
      "node_modules/usepath/package.json": '{ "name": "usepath", "main": "index.js" }',
      "node_modules/usepath/index.js":
        'const { basename } = require("node:path");\nmodule.exports = function () { return basename("/a/b/c.txt"); };',
    };
    expectSameOutputAsNode(source, { files, name: "cjs-builtin", extensions: true });
  });
});
