import { describe, expect, it } from "vitest";
import { Parser } from "../../src/parser/parser.js";
import { SourceFile } from "../../src/diagnostics/source.js";
import { DiagnosticBag } from "../../src/diagnostics/diagnostic.js";
import { collectUsage } from "../../src/driver/usage.js";
import type { ProgramUsage } from "../../src/extensions/link-feature.js";

/** Parse `source` and collect its usage facts. */
function usage(source: string): ProgramUsage {
  const bag = new DiagnosticBag();
  const file = new SourceFile("usage.ts", source);
  const ast = new Parser(file, bag).parseSourceFile();
  expect(bag.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);
  return collectUsage(ast);
}

describe("collectUsage strings and identifiers", () => {
  it("collects string literals, template chunks and HTML assets", () => {
    const facts = usage(`
      const page = \`<div class="a">\${1}</div>\`;
      const tag = "<video>";
      document.title = "webgl2";
    `);
    expect(facts.strings).toContain('<div class="a">');
    expect(facts.strings).toContain("</div>");
    expect(facts.strings).toContain("<video>");
    expect(facts.strings).toContain("webgl2");
    expect(facts.identifiers.has("document")).toBe(true);
  });

  it("resolves a const alias for a markup sink", () => {
    const facts = usage(`
      const page = "<video src='x.mp4'></video>";
      win.loadHTML(page);
    `);
    expect(facts.dynamicMarkup).toBe(false);
    expect(facts.strings.some((value) => value.includes("<video"))).toBe(true);
  });

  it("resolves a template whose substitutions are literals", () => {
    const facts = usage(`
      const size = 320;
      win.loadHTML(\`<video width="\${size}"></video>\`);
    `);
    expect(facts.dynamicMarkup).toBe(false);
    expect(facts.strings.some((value) => value.includes("<video"))).toBe(true);
  });

  it("treats a runtime sink as dynamic markup", () => {
    expect(usage(`win.loadHTML(buildPage());`).dynamicMarkup).toBe(true);
    expect(usage(`el.innerHTML = fetched;`).dynamicMarkup).toBe(true);
    expect(usage(`el.outerHTML = \`<div>\${data}</div>\`;`).dynamicMarkup).toBe(true);
    expect(usage(`document.createElement(tag);`).dynamicMarkup).toBe(true);
    expect(usage(`document.write(page);`).dynamicMarkup).toBe(true);
  });

  it("does not call a literal innerHTML dynamic", () => {
    expect(usage(`el.innerHTML = "<b>hi</b>";`).dynamicMarkup).toBe(false);
  });

  it("treats setAttribute style values as markup when they are computed", () => {
    expect(usage(`el.setAttribute("style", css);`).dynamicMarkup).toBe(true);
    expect(usage(`el.setAttribute("style", "mix-blend-mode: multiply");`).dynamicMarkup).toBe(false);
    expect(usage(`el.setAttribute(name, css);`).dynamicMarkup).toBe(true);
  });

  it("treats a computed CSS property name as dynamic", () => {
    expect(usage(`el.style.setProperty(prop, "multiply");`).dynamicMarkup).toBe(true);
    expect(usage(`el.style.setProperty("mix-blend-mode", "multiply");`).dynamicMarkup).toBe(false);
  });

  it("treats a run-time <style> text as dynamic CSS", () => {
    const facts = usage(`
      const style = document.createElement("style");
      style.textContent = css;
    `);
    expect(facts.dynamicMarkup).toBe(true);
  });

  it("ignores a non-const binding it cannot prove", () => {
    expect(usage(`let page = "<video>"; win.loadHTML(page);`).dynamicMarkup).toBe(true);
  });
});

describe("collectUsage getContext kinds", () => {
  it("records literal and aliased context kinds", () => {
    const facts = usage(`
      const kind = "webgl2";
      canvas.getContext("2d");
      canvas.getContext(kind);
      canvas["getContext"]("webgl");
    `);
    expect(facts.getContext).toEqual(["2d", "webgl2", "webgl"]);
  });

  it("records an unknown kind when it is decided at run time", () => {
    const facts = usage(`canvas.getContext(kind);`);
    expect(facts.getContext).toEqual([undefined]);
  });
});
