import { describe, expect, it } from "vitest";
import { loadHtmlAsset } from "../../src/extensions/gui/html.js";
import { guiExtension } from "../../src/extensions/gui/index.js";
import { DiagnosticBag } from "../../src/diagnostics/diagnostic.js";
import { SourceFile } from "../../src/diagnostics/source.js";
import { Parser } from "../../src/parser/parser.js";
import { hashString } from "../../src/driver/cache.js";

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

  it("leaves external (src) scripts and empty bodies untouched", () => {
    const src = loadHtmlAsset("a.html", `<script src="./app.ts"></script>`);
    expect(htmlOf(src.moduleSource)).toContain('src="./app.ts"');
    expect(src.moduleSource).not.toContain("__registerScript");
    const empty = loadHtmlAsset("b.html", `<script></script>`);
    expect(empty.moduleSource).not.toContain("__registerScript");
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
});
