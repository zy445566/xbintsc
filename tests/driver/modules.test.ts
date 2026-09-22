import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
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
  for (const [name, content] of Object.entries(files)) writeFileSync(join(directory, name), content);
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
