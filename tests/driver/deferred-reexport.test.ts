/**
 * Re-export lowering for deferred (`<script src>`) modules.
 *
 * A deferred module's top-level bindings live inside its own wrapper function,
 * so a re-export of another deferred module must read the value from that
 * module's wrapper — never from the dependency's final (renamed) name, which is
 * wrapper-local and does not exist at top level.
 */

import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { bundleModules } from "../../src/driver/modules.js";
import { DiagnosticBag } from "../../src/diagnostics/diagnostic.js";
import { bind } from "../../src/binder/binder.js";
import { checkUnresolvedNames } from "../../src/checker/undefined.js";

const directories: string[] = [];

afterEach(() => {
  for (const directory of directories.splice(0)) rmSync(directory, { recursive: true, force: true });
});

/** Create a throwaway directory tree from `{ relativePath: contents }`. */
function writeFiles(files: Record<string, string>): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-deferred-reexport-"));
  directories.push(directory);
  for (const [name, content] of Object.entries(files)) {
    const path = join(directory, name);
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, content);
  }
  return directory;
}

/** Bundle an entry with a single deferred-marker import and name-check the result. */
function unresolvedNames(files: Record<string, string>): string[] {
  const directory = writeFiles(files);
  const bag = new DiagnosticBag();
  const result = bundleModules(join(directory, "main.ts"), bag, new Set());
  expect(bag.hasErrors).toBe(false);
  const binding = bind(result!.sourceFile);
  const checkerBag = new DiagnosticBag();
  checkUnresolvedNames(result!.sourceFile, binding, checkerBag, new Set(["__xt_defer"]));
  return checkerBag.diagnostics.map((diagnostic) => diagnostic.message);
}

describe("deferred module re-exports", () => {
  it("reads `export *` through the dependency's wrapper", () => {
    expect(
      unresolvedNames({
        "value.ts": 'export const value = "v";',
        "wide.ts": 'export * from "./value";',
        "app.ts": 'import { value } from "./wide";\nconsole.log(value);',
        "main.ts": 'import { __xt_deferred_module as __xt_deferred_0_0 } from "./app.ts";',
      }),
    ).toEqual([]);
  });

  it("reads a named re-export through the dependency's wrapper", () => {
    expect(
      unresolvedNames({
        "value.ts": 'export const value = "v";',
        "named.ts": 'export { value as alias } from "./value";',
        "app.ts": 'import { alias } from "./named";\nconsole.log(alias);',
        "main.ts": 'import { __xt_deferred_module as __xt_deferred_0_0 } from "./app.ts";',
      }),
    ).toEqual([]);
  });
});
