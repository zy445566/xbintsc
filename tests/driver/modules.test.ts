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
