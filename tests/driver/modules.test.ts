import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { bundleModules } from "../../src/driver/modules.js";
import { DiagnosticBag, DiagnosticCode } from "../../src/diagnostics/diagnostic.js";
import { walk } from "../../src/ast/visitor.js";
import { SyntaxKind, type Node } from "../../src/ast/nodes.js";

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-modules-"));
  directories.push(directory);
  return directory;
}

function writeFiles(files: Record<string, string>): string {
  const directory = temporaryDirectory();
  for (const [name, content] of Object.entries(files)) {
    const path = join(directory, name);
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, content);
  }
  return directory;
}

function bundle(entry: string): { result: ReturnType<typeof bundleModules>; bag: DiagnosticBag } {
  const bag = new DiagnosticBag();
  const result = bundleModules(entry, bag);
  return { result, bag };
}

function identifierTexts(node: Node): string[] {
  const texts: string[] = [];
  walk(node, (current) => {
    if (current.kind === SyntaxKind.Identifier) texts.push((current as unknown as { text: string }).text);
  });
  return texts;
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

describe("bundleModules", () => {
  it("loads dependencies before the entry module and renames top-level bindings", () => {
    const directory = writeFiles({
      "util.ts": "export function add(a: number, b: number) { return a + b; }",
      "main.ts": 'import { add } from "./util";\nconsole.log(add(1, 2));',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    expect(result?.moduleCount).toBe(2);
    const names = identifierTexts(result!.sourceFile);
    // Both the declaration and its use get the unique module-prefixed name.
    const renamed = names.filter((name) => name.endsWith("add"));
    expect(renamed.length).toBeGreaterThanOrEqual(2);
    expect(new Set(renamed).size).toBe(1);
  });

  it("rewrites default imports to the dependency's default export", () => {
    const directory = writeFiles({
      "util.ts": "export default function make() { return 1; }",
      "main.ts": 'import make from "./util";\nmake();',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    const names = identifierTexts(result!.sourceFile);
    expect(names.some((name) => name.endsWith("make"))).toBe(true);
  });

  it("lowers `export default <expression>` into a synthetic const", () => {
    const directory = writeFiles({ "main.ts": "export default 42;" });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    const statements = result!.sourceFile.statements;
    expect(statements).toHaveLength(1);
    expect(statements[0]!.kind).toBe(SyntaxKind.VariableStatement);
    const names = identifierTexts(result!.sourceFile);
    expect(names.some((name) => name.endsWith("default"))).toBe(true);
  });

  it("lowers namespace imports into an object literal", () => {
    const directory = writeFiles({
      "util.ts": "export const x = 1;\nexport function add() { return 1; }",
      "main.ts": 'import * as util from "./util";\nutil.x;',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    let hasObjectLiteral = false;
    walk(result!.sourceFile, (node) => {
      if (node.kind === SyntaxKind.ObjectLiteralExpression) hasObjectLiteral = true;
    });
    expect(hasObjectLiteral).toBe(true);
  });

  it("resolves named re-exports from another module", () => {
    const directory = writeFiles({
      "math.ts": "export function add() { return 1; }",
      "index.ts": 'export { add as plus } from "./math";',
      "main.ts": 'import { plus } from "./index";\nplus();',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    expect(result?.moduleCount).toBe(3);
  });

  it("resolves `export *` re-exports", () => {
    const directory = writeFiles({
      "math.ts": "export const two = 2;",
      "index.ts": 'export * from "./math";',
      "main.ts": 'import { two } from "./index";\ntwo;',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    expect(result?.moduleCount).toBe(3);
  });

  it("keeps external (extension) imports in place", () => {
    const directory = writeFiles({
      "main.ts": 'import fs from "node:fs";\nfs.readFileSync("f.txt");',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    expect(result!.sourceFile.statements[0]!.kind).toBe(SyntaxKind.ImportDeclaration);
  });

  it("bundles a package resolved from node_modules", () => {
    const directory = writeFiles({
      "node_modules/mathx/package.json": '{ "name": "mathx", "main": "index.js" }',
      "node_modules/mathx/index.js": "export function add(a, b) { return a + b; }\nexport const two = 2;",
      "main.ts": 'import { add, two } from "mathx";\nadd(two, 1);',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    expect(result?.moduleCount).toBe(2);
    // The package import is lowered away; only the merged statements remain.
    expect(result!.sourceFile.statements.some((s) => s.kind === SyntaxKind.ImportDeclaration)).toBe(false);
  });

  it("resolves scoped packages, subpaths and the exports map", () => {
    const directory = writeFiles({
      "node_modules/@scope/tool/package.json":
        '{ "name": "@scope/tool", "exports": { ".": { "import": "./dist/index.js" }, "./extra": "./dist/extra.js" } }',
      "node_modules/@scope/tool/dist/index.js": 'export const name = "scoped";',
      "node_modules/@scope/tool/dist/extra.js": "export const extra = 42;",
      "main.ts": 'import { name } from "@scope/tool";\nimport { extra } from "@scope/tool/extra";\nname;\nextra;',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    expect(result?.moduleCount).toBe(3);
  });

  it("prefers a known platform module over a same-named package", () => {
    const directory = writeFiles({
      "node_modules/fs/package.json": '{ "name": "fs", "main": "index.js" }',
      "node_modules/fs/index.js": "export const fake = true;",
      "main.ts": 'import fs from "fs";\nfs;',
    });
    const bag = new DiagnosticBag();
    const result = bundleModules(join(directory, "main.ts"), bag, new Set(["fs"]));
    expect(bag.hasErrors).toBe(false);
    // The platform `fs` import is left for code generation, not bundled.
    expect(result!.sourceFile.statements[0]!.kind).toBe(SyntaxKind.ImportDeclaration);
  });

  it("leaves an unresolvable bare specifier for the generator", () => {
    const directory = writeFiles({
      "main.ts": 'import { z } from "zod";\nz;',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    expect(result!.sourceFile.statements[0]!.kind).toBe(SyntaxKind.ImportDeclaration);
  });

  it("terminates on a circular import graph", () => {
    const directory = writeFiles({
      "a.ts": 'import { b } from "./b";\nexport const a = 1;',
      "b.ts": 'import { a } from "./a";\nexport const b = 2;',
    });
    const { result, bag } = bundle(join(directory, "a.ts"));
    // Modules are cached before their imports are walked, so a cycle is bundled
    // once rather than recursing forever.
    expect(bag.hasErrors).toBe(false);
    expect(result?.moduleCount).toBe(2);
  });

  it("resolves an extension-less import to a `.js` source", () => {
    const directory = writeFiles({
      "util.js": "export function add(a, b) { return a + b; }",
      "main.js": 'import { add } from "./util";\nconsole.log(add(1, 2));',
    });
    const { result, bag } = bundle(join(directory, "main.js"));
    expect(bag.hasErrors).toBe(false);
    expect(result?.moduleCount).toBe(2);
  });

  it("prefers a `.ts` source over a `.js` sibling for an extension-less import", () => {
    const directory = writeFiles({
      "util.ts": "export const kind = \"ts\";",
      "util.js": 'export const kind = "js";',
      "main.ts": 'import { kind } from "./util";\nkind;',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    expect(result?.moduleCount).toBe(2);
    // The `.ts` source is bundled, so the `.js` sibling's string is absent.
    expect(result!.text).toContain('"ts"');
    expect(result!.text).not.toContain('"js"');
  });

  it("maps `.mjs`/`.cjs` specifiers to `.mts`/`.cts` sources", () => {
    const directory = writeFiles({
      "a.mts": "export const a = 1;",
      "b.cts": "export const b = 2;",
      "main.ts": 'import { a } from "./a.mjs";\nimport { b } from "./b.cjs";\na;\nb;',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    expect(result?.moduleCount).toBe(3);
  });

  it("bundles a `.js` entry module through extension-less imports", () => {
    const directory = writeFiles({
      "math.js": "export const two = 2;",
      "main.js": 'import { two } from "./math";\nconsole.log(two);',
    });
    const { result, bag } = bundle(join(directory, "main.js"));
    expect(bag.hasErrors).toBe(false);
    expect(result?.moduleCount).toBe(2);
  });

  it("reports a dependency that cannot be resolved", () => {
    const directory = writeFiles({ "main.ts": 'import { x } from "./missing";\nx;' });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(result).toBeUndefined();
    expect(bag.diagnostics.some((d) => d.message.includes("Cannot resolve module"))).toBe(true);
  });

  it("reports an entry file that does not exist", () => {
    const directory = temporaryDirectory();
    const { result, bag } = bundle(join(directory, "nope.ts"));
    expect(result).toBeUndefined();
    expect(bag.diagnostics.some((d) => d.code === DiagnosticCode.CodegenError)).toBe(true);
    expect(bag.diagnostics.some((d) => d.message.includes("Cannot find module"))).toBe(true);
  });
});

describe("CommonJS in node_modules", () => {
  function requireCalls(node: Node): number {
    let count = 0;
    walk(node, (current) => {
      if (current.kind !== SyntaxKind.CallExpression) return;
      const callee = (current as unknown as { expression: Node }).expression;
      if (callee.kind === SyntaxKind.Identifier && (callee as unknown as { text: string }).text === "require") count++;
    });
    return count;
  }

  it("lowers a require of another CommonJS package", () => {
    const directory = writeFiles({
      "node_modules/leftpad/package.json": '{ "name": "leftpad", "main": "index.js" }',
      "node_modules/leftpad/index.js": 'exports.pad = function (s, n) { return s; };',
      "node_modules/usecjs/package.json": '{ "name": "usecjs", "main": "index.js" }',
      "node_modules/usecjs/index.js": 'var lp = require("leftpad");\nmodule.exports.run = function () { return lp.pad("x", 1); };',
      "main.ts": 'import { run } from "usecjs";\nconsole.log(run());',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    expect(result?.moduleCount).toBe(3);
    // The `require` call is rewritten away.
    expect(requireCalls(result!.sourceFile)).toBe(0);
    // Each `node_modules` module gets its own `exports` state.
    const names = identifierTexts(result!.sourceFile);
    expect(new Set(names.filter((name) => name.endsWith("$cjs_module"))).size).toBe(2);
  });

  it("exposes CommonJS named exports to ESM imports", () => {
    const directory = writeFiles({
      "node_modules/n/package.json": '{ "name": "n", "main": "index.js" }',
      "node_modules/n/index.js": "exports.foo = 1;\nexports.bar = function () { return 2; };",
      "main.ts": 'import { foo, bar } from "n";\nconsole.log(foo, bar());',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    const names = identifierTexts(result!.sourceFile);
    expect(names.some((name) => name.endsWith("$cjs_foo"))).toBe(true);
    expect(names.some((name) => name.endsWith("$cjs_bar"))).toBe(true);
  });

  it("exposes the whole `module.exports` object as the default export", () => {
    const directory = writeFiles({
      "node_modules/obj/package.json": '{ "name": "obj", "main": "index.js" }',
      "node_modules/obj/index.js": 'module.exports = { a: 1, b: 2 };',
      "main.ts": 'import o from "obj";\nconsole.log(o.a);',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    expect(identifierTexts(result!.sourceFile).some((name) => name.endsWith("$cjs_default"))).toBe(true);
  });

  it("exposes a required ESM module as a synthetic namespace object", () => {
    const directory = writeFiles({
      "node_modules/c/package.json": '{ "name": "c", "main": "index.js" }',
      "node_modules/c/index.js": 'var e = require("./dep.js");\nmodule.exports = function () { return e.hello(); };',
      "node_modules/c/dep.js": 'export function hello() { return "hi"; }',
      "main.ts": 'import f from "c";\nconsole.log(f());',
    });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(bag.hasErrors).toBe(false);
    let hasObjectLiteral = false;
    walk(result!.sourceFile, (node) => {
      if (node.kind === SyntaxKind.ObjectLiteralExpression) hasObjectLiteral = true;
    });
    expect(hasObjectLiteral).toBe(true);
    expect(requireCalls(result!.sourceFile)).toBe(0);
  });

  it("hoists an external require into an ESM import", () => {
    const directory = writeFiles({
      "node_modules/useos/package.json": '{ "name": "useos", "main": "index.js" }',
      "node_modules/useos/index.js": 'var os = require("node:os");\nmodule.exports = function () { return os.platform(); };',
      "main.ts": 'import f from "useos";\nconsole.log(f());',
    });
    const bag = new DiagnosticBag();
    const result = bundleModules(join(directory, "main.ts"), bag, new Set(["node:os"]));
    expect(bag.hasErrors).toBe(false);
    const imports = result!.sourceFile.statements.filter((s) => s.kind === SyntaxKind.ImportDeclaration);
    expect(imports.length).toBe(1);
    expect(requireCalls(result!.sourceFile)).toBe(0);
  });

  it("hoists a destructured external require into named imports", () => {
    const directory = writeFiles({
      "node_modules/usepath/package.json": '{ "name": "usepath", "main": "index.js" }',
      "node_modules/usepath/index.js": 'const { basename } = require("node:path");\nmodule.exports = function () { return basename("/a/b"); };',
      "main.ts": 'import f from "usepath";\nconsole.log(f());',
    });
    const bag = new DiagnosticBag();
    const result = bundleModules(join(directory, "main.ts"), bag, new Set(["node:path"]));
    expect(bag.hasErrors).toBe(false);
    expect(result!.sourceFile.statements.filter((s) => s.kind === SyntaxKind.ImportDeclaration).length).toBe(1);
  });

  it("leaves `require` in user code untouched", () => {
    const directory = writeFiles({ "main.ts": 'var x = require("./missing");\nconsole.log(x);' });
    const { result, bag } = bundle(join(directory, "main.ts"));
    expect(result?.moduleCount).toBe(1);
    expect(requireCalls(result!.sourceFile)).toBe(1);
    expect(bag.diagnostics.some((d) => d.code === DiagnosticCode.UnsupportedFeature)).toBe(false);
  });

  it("reports a require with a non-literal argument", () => {
    const directory = writeFiles({
      "node_modules/dyn/package.json": '{ "name": "dyn", "main": "index.js" }',
      "node_modules/dyn/index.js": 'var name = "leftpad";\nmodule.exports = require(name);',
      "main.ts": 'import x from "dyn";\nconsole.log(x);',
    });
    const { bag } = bundle(join(directory, "main.ts"));
    expect(bag.diagnostics.some((d) => d.code === DiagnosticCode.UnsupportedFeature)).toBe(true);
  });
});

describe("asset loaders", () => {
  it("rewrites a matching asset extension into a TS module", () => {
    const directory = writeFiles({
      "data.foo": "hello",
      "main.ts": 'import value from "./data.foo";\nconsole.log(value);',
    });
    const seen: Array<[string, string]> = [];
    const loader = (path: string, source: string) => {
      seen.push([path, source]);
      return { moduleSource: `export default ${JSON.stringify(source)};` };
    };
    const bag = new DiagnosticBag();
    const result = bundleModules(join(directory, "main.ts"), bag, new Set(), { ".foo": loader });
    expect(bag.hasErrors).toBe(false);
    expect(seen).toEqual([[join(directory, "data.foo"), "hello"]]);
    expect(result?.text).toContain('export default "hello"');
  });

  it("follows imports from a loader-generated module", () => {
    const directory = writeFiles({
      "widget.html": "<div></div>",
      "helper.ts": "export const n = 41;",
      "main.ts": 'import html from "./widget.html";\nconsole.log(html);',
    });
    const loader = () => ({
      moduleSource: 'import { n } from "./helper";\nexport default "loaded-" + n;',
    });
    const bag = new DiagnosticBag();
    const result = bundleModules(join(directory, "main.ts"), bag, new Set(), { ".html": loader });
    expect(bag.hasErrors).toBe(false);
    expect(result?.moduleCount).toBe(3);
  });

  it("leaves files with other extensions untouched", () => {
    const directory = writeFiles({ "main.ts": "console.log(1);" });
    const loader = () => ({ moduleSource: 'throw new Error("should not run");' });
    const bag = new DiagnosticBag();
    bundleModules(join(directory, "main.ts"), bag, new Set(), { ".html": loader });
    expect(bag.hasErrors).toBe(false);
  });

  it("reports a loader that throws as a build diagnostic", () => {
    const directory = writeFiles({
      "data.foo": "boom",
      "main.ts": 'import value from "./data.foo";\nconsole.log(value);',
    });
    const loader = () => {
      throw new Error("loader exploded");
    };
    const bag = new DiagnosticBag();
    const result = bundleModules(join(directory, "main.ts"), bag, new Set(), { ".foo": loader });
    expect(result).toBeUndefined();
    expect(bag.hasErrors).toBe(true);
    expect(bag.diagnostics.some((diagnostic) => diagnostic.message.includes("loader exploded"))).toBe(true);
  });

  it("reports an unresolvable relative import", () => {
    const directory = writeFiles({ "main.ts": 'import value from "./missing";\nconsole.log(value);' });
    const bag = new DiagnosticBag();
    const result = bundleModules(join(directory, "main.ts"), bag);
    expect(result).toBeUndefined();
    expect(bag.hasErrors).toBe(true);
    expect(bag.diagnostics.some((diagnostic) => diagnostic.message.includes("Cannot resolve module"))).toBe(true);
  });

  it("reports a missing entry file", () => {
    const directory = writeFiles({ "main.ts": "console.log(1);" });
    const bag = new DiagnosticBag();
    const result = bundleModules(join(directory, "nope.ts"), bag);
    expect(result).toBeUndefined();
    expect(bag.diagnostics.some((diagnostic) => diagnostic.message.includes("Cannot find module"))).toBe(true);
  });
});
