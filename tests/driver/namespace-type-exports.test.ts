/**
 * A module namespace object holds *runtime* exports only.
 *
 * `import * as ns from "./mod"` is lowered to an object literal built from the
 * module's export table. An interface or a type alias has no runtime slot to
 * read, so it must never appear there (doing so made every namespace import of
 * a module that exported an interface fail with an internal codegen error).
 */

import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { bundleModules } from "../../src/driver/modules.js";
import { DiagnosticBag } from "../../src/diagnostics/diagnostic.js";
import { walk } from "../../src/ast/visitor.js";
import { SyntaxKind, type Node } from "../../src/ast/nodes.js";

const directories: string[] = [];

function writeFiles(files: Record<string, string>): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-namespace-types-"));
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

/** Every property name written into an object literal in the bundled output. */
function objectLiteralKeys(node: Node): string[] {
  const keys: string[] = [];
  walk(node, (current) => {
    if (current.kind !== SyntaxKind.ObjectLiteralExpression) return;
    const properties = (current as unknown as { properties: Node[] }).properties;
    for (const property of properties) {
      const name = (property as { name?: { text: string } }).name;
      if (name) keys.push(name.text);
    }
  });
  return keys;
}

describe("namespace object exports", () => {
  it("leaves interfaces and type aliases out", () => {
    const directory = writeFiles({
      "util.ts": "export interface Options { a?: number; }\nexport type Kind = string;\nexport const x = 1;",
      "main.ts": 'import * as util from "./util";\nutil.x;',
    });
    const bag = new DiagnosticBag();
    const result = bundleModules(join(directory, "main.ts"), bag);
    expect(bag.hasErrors).toBe(false);
    expect(objectLiteralKeys(result!.sourceFile)).toEqual(["x"]);
  });
});
