import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { loadHtmlAsset } from "../../src/extensions/gui/html.js";
import { guiExtension } from "../../src/extensions/gui/index.js";
import { DiagnosticBag } from "../../src/diagnostics/diagnostic.js";
import { SourceFile } from "../../src/diagnostics/source.js";
import { Parser } from "../../src/parser/parser.js";
import { hashString } from "../../src/driver/cache.js";

const directories: string[] = [];

/** Write a temp tree of files and return its root directory. */
function writeFiles(files: Record<string, string>): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-gui-loader-"));
  directories.push(directory);
  for (const [name, contents] of Object.entries(files)) {
    const path = join(directory, name);
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, contents);
  }
  return directory;
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

/** Parse a generated module and return its diagnostics. */
function parseDiagnostics(source: string): number {
  const bag = new DiagnosticBag();
  new Parser(new SourceFile("generated.ts", source), bag).parseSourceFile();
  return bag.diagnostics.filter((diagnostic) => diagnostic.category === "error").length;
}

/** The rewritten HTML string a generated module default-exports. */
function htmlOf(moduleSource: string): string {
  const marker = "export default ";
  const start = moduleSource.lastIndexOf(marker) + marker.length;
  return JSON.parse(moduleSource.slice(start).replace(/;\s*$/, "")) as string;
}

describe("gui .html asset loader", () => {
  it("is registered by the gui extension", () => {
    expect(guiExtension.assetLoaders?.()[".html"]).toBe(loadHtmlAsset);
  });

  it("compiles an inline script into a registered function and a marker", () => {
    const body = "const n = 1;\nconsole.log(n);";
    const id = hashString(body);
    const { moduleSource } = loadHtmlAsset(
      "page.html",
      `<div id="a"></div><script>${body}</script>`,
    );
    expect(moduleSource).toContain(`function __xt_script_${id}(window: any, document: any): void {`);
    expect(moduleSource).toContain(`__registerScript(${JSON.stringify(id)}, __xt_script_${id});`);
    expect(htmlOf(moduleSource)).toContain(`<script data-xt-id="${id}"></script>`);
    expect(htmlOf(moduleSource)).not.toContain(body);
    expect(moduleSource).toContain("export default");
    expect(parseDiagnostics(moduleSource)).toBe(0);
  });

  it("accepts lang=ts and type=module, but ignores data blocks", () => {
    const ts = loadHtmlAsset("a.html", `<script lang="ts">let x = 1;</script>`);
    expect(ts.moduleSource).toContain("__xt_script_");
    const module = loadHtmlAsset("b.html", `<script type="module">let y = 2;</script>`);
    expect(module.moduleSource).toContain("__xt_script_");
    const json = loadHtmlAsset("c.html", `<script type="application/json">{"a":1}</script>`);
    expect(json.moduleSource).not.toContain("__xt_script_");
    expect(json.moduleSource).toContain("application/json");
  });

  it("leaves URL src scripts and empty bodies untouched", () => {
    const src = loadHtmlAsset("a.html", `<script src="https://cdn.example/app.js"></script>`);
    expect(htmlOf(src.moduleSource)).toContain('src="https://cdn.example/app.js"');
    expect(src.moduleSource).not.toContain("__registerScript");
    const empty = loadHtmlAsset("b.html", `<script></script>`);
    expect(empty.moduleSource).not.toContain("__registerScript");
  });

  it("compiles an external <script src> file into a registered function", () => {
    const directory = writeFiles({
      "app.ts": "const b = document.getElementById('b');\nb.textContent = 'ready';",
    });
    const htmlPath = join(directory, "page.html");
    const body = "const b = document.getElementById('b');\nb.textContent = 'ready';";
    const id = hashString(body);
    const { moduleSource } = loadHtmlAsset(htmlPath, `<script src="./app.ts"></script>`);
    expect(moduleSource).toContain(`function __xt_script_${id}(window: any, document: any): void {`);
    expect(moduleSource).toContain("b.textContent = 'ready';");
    expect(htmlOf(moduleSource)).toContain(`<script data-xt-id="${id}"></script>`);
    expect(parseDiagnostics(moduleSource)).toBe(0);
  });

  it("hoists imports from an external script, rewriting them from the HTML dir", () => {
    const directory = writeFiles({
      "scripts/app.ts": 'import { size } from "../lib/util";\nconsole.log(size);',
      "lib/util.ts": "export const size = 1;",
    });
    const htmlPath = join(directory, "page.html");
    const { moduleSource } = loadHtmlAsset(htmlPath, `<script src="./scripts/app.ts"></script>`);
    expect(moduleSource).toContain('import { size } from "./lib/util"');
    expect(moduleSource).toContain("console.log(size);");
    expect(moduleSource).not.toContain("../lib/util");
    expect(parseDiagnostics(moduleSource)).toBe(0);
  });

  it("strips export modifiers from an external script", () => {
    const directory = writeFiles({
      "app.ts": "export const a = 1;\nexport function f() { return a; }",
    });
    const htmlPath = join(directory, "page.html");
    const { moduleSource } = loadHtmlAsset(htmlPath, `<script src="./app.ts"></script>`);
    expect(moduleSource).toContain("const a = 1;");
    expect(moduleSource).not.toContain("export const");
    expect(parseDiagnostics(moduleSource)).toBe(0);
  });

  it("reports a missing external script", () => {
    const directory = writeFiles({ "page.html": "" });
    expect(() => loadHtmlAsset(join(directory, "page.html"), `<script src="./missing.ts"></script>`)).toThrow(
      /not found/,
    );
  });

  it("rejects colliding imported bindings across external scripts", () => {
    const directory = writeFiles({
      "a.ts": 'import { x } from "./x";\nconsole.log(x);',
      "b.ts": 'import { x } from "./y";\nconsole.log(x);',
      "x.ts": "export const x = 1;",
      "y.ts": "export const x = 2;",
    });
    const htmlPath = join(directory, "page.html");
    expect(() =>
      loadHtmlAsset(htmlPath, `<script src="./a.ts"></script><script src="./b.ts"></script>`),
    ).toThrow(/already imported/);
  });

  it("emits only the HTML string when there are no scripts", () => {
    const { moduleSource } = loadHtmlAsset("page.html", "<h1>hi</h1>");
    expect(moduleSource).toBe('export default "<h1>hi</h1>";');
    expect(parseDiagnostics(moduleSource)).toBe(0);
  });

  it("compiles every script body in document order", () => {
    const first = "let a = 1;";
    const second = "let b = 2;";
    const { moduleSource } = loadHtmlAsset(
      "page.html",
      `<script>${first}</script><script>${second}</script>`,
    );
    expect(moduleSource.indexOf(hashString(first))).toBeLessThan(moduleSource.indexOf(hashString(second)));
    expect(parseDiagnostics(moduleSource)).toBe(0);
  });

  it("hoists namespace, side-effect and re-export statements", () => {
    const directory = writeFiles({
      "app.ts": [
        'import * as util from "./util";',
        'import "./side-effect";',
        'import defaultExport, { named } from "./mixed";',
        'const a = 1;',
        'export { a };',
        'export { x } from "./x";',
        'export * from "./y";',
        'export default 42;',
        'export default function defaultFn() { return 0; }',
        'console.log(util, defaultExport, named);',
      ].join("\n"),
    });
    const htmlPath = join(directory, "page.html");
    const { moduleSource } = loadHtmlAsset(htmlPath, `<script src="./app.ts"></script>`);
    expect(moduleSource).toContain('import * as util from "./util"');
    expect(moduleSource).toContain('import {} from "./side-effect"');
    expect(moduleSource).toContain('import defaultExport, { named } from "./mixed"');
    expect(moduleSource).toContain('export { x } from "./x"');
    expect(moduleSource).toContain('export * from "./y"');
    /* The local `export { a }` and `export default` are blanked out. */
    expect(moduleSource).not.toContain("export default 42");
    expect(moduleSource).not.toContain("export default function");
    expect(moduleSource).toContain("function defaultFn()");
    expect(moduleSource).toContain("console.log(util, defaultExport, named);");
    expect(parseDiagnostics(moduleSource)).toBe(0);
  });

  it("hoists a bare (non-relative) import unchanged", () => {
    const directory = writeFiles({ "app.ts": 'import "pkg";\nconsole.log(1);' });
    const htmlPath = join(directory, "page.html");
    const { moduleSource } = loadHtmlAsset(htmlPath, `<script src="./app.ts"></script>`);
    expect(moduleSource).toContain('import {} from "pkg"');
    expect(parseDiagnostics(moduleSource)).toBe(0);
  });

  it("reads single-quoted, unquoted and query-string src attributes", () => {
    const directory = writeFiles({ "app.ts": "console.log(1);" });
    const htmlPath = join(directory, "page.html");
    for (const src of ["'./app.ts'", "./app.ts", '"./app.ts?v=1#frag"']) {
      const { moduleSource } = loadHtmlAsset(htmlPath, `<script src=${src}></script>`);
      expect(moduleSource, src).toContain("__registerScript");
      expect(parseDiagnostics(moduleSource), src).toBe(0);
    }
  });
});
