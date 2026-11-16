/**
 * A CommonJS module's `module`/`exports` pair must exist before any module body
 * runs.
 *
 * Node hands a module its pair when it starts loading, so in a `require` cycle
 * the earlier module can read the later one's (partly filled) object. When the
 * pair lived at the top of the module's own block it was declared *after* the
 * reader in a cycle, and the reader threw
 * `Cannot read properties of undefined (reading 'exports')`.
 */

import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { bundleModules } from "../../src/driver/modules.js";
import { DiagnosticBag } from "../../src/diagnostics/diagnostic.js";
import { SyntaxKind, type Node } from "../../src/ast/nodes.js";

const directories: string[] = [];

function writeFiles(files: Record<string, string>): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-cjs-prelude-"));
  directories.push(directory);
  for (const [name, content] of Object.entries(files)) {
    const path = join(directory, name);
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, content);
  }
  return directory;
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

/** The name a top-level statement declares, when it declares one identifier. */
function declaredName(statement: Node): string | undefined {
  if (statement.kind !== SyntaxKind.VariableStatement) return undefined;
  const declaration = (statement as unknown as { declarationList: { declarations: Node[] } }).declarationList
    .declarations[0];
  const name = (declaration as unknown as { name?: Node }).name;
  if (!name || name.kind !== SyntaxKind.Identifier) return undefined;
  return (name as unknown as { text: string }).text;
}

describe("CommonJS module prelude", () => {
  it("declares every module object above the first module body", () => {
    // `a.js` and `b.js` require each other, so one of the two blocks is
    // concatenated after the other's top-level `require` read.
    const directory = writeFiles({
      "node_modules/fake/a.js": 'require("./b.js");\nexports.a = 1;\n',
      "node_modules/fake/b.js": 'const a = require("./a.js");\nexports.seen = a.a;\n',
      "main.ts": 'import value from "fake/a.js";\nconsole.log(value);\n',
    });
    const bag = new DiagnosticBag();
    const result = bundleModules(join(directory, "main.ts"), bag);
    expect(bag.hasErrors).toBe(false);
    const statements = result!.sourceFile.statements;
    const prelude: string[] = [];
    for (const statement of statements) {
      const name = declaredName(statement);
      if (name === undefined || !/^m\d+_\$cjs_(module|exports)$/.test(name)) break;
      prelude.push(name);
    }
    // Both modules contribute their pair before anything else runs.
    expect(prelude).toHaveLength(4);
    expect(prelude.filter((name) => name.endsWith("_$cjs_module"))).toHaveLength(2);
  });
});
