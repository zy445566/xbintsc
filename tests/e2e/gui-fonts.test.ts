/**
 * GUI e2e: CSS `font-family` / `font-weight` select real installed faces.
 *
 * The assertions are made through layout widths, which is what a page actually
 * sees: a bold face with wider advances makes the inline-block wider, and two
 * different families lay the same text out differently. The suites pick a
 * family whose bold face exists on the host, so a host without one skips them
 * instead of asserting a synthetic-bold behaviour the engine does not have.
 */

import { existsSync } from "node:fs";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

interface FontFixture {
  sans: string;
  serif: string;
}

function anyExists(paths: readonly string[]): boolean {
  return paths.some((path) => existsSync(path));
}

/** A family whose regular and bold faces are both present, plus a distinct
 * serif family for the family-selection test. */
function detectFonts(): FontFixture | undefined {
  let sans = "";
  if (anyExists(["C:\\Windows\\Fonts\\arialbd.ttf"])) sans = "Arial";
  else if (anyExists(["/System/Library/Fonts/Supplemental/Arial Bold.ttf"])) sans = "Arial";
  else if (anyExists(["/System/Library/Fonts/Helvetica.ttc"])) sans = "Helvetica";
  else if (
    anyExists([
      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    ])
  ) {
    sans = "DejaVu Sans";
  } else if (
    anyExists([
      "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
    ])
  ) {
    sans = "Liberation Sans";
  }
  if (sans === "") return undefined;

  let serif = "";
  if (anyExists(["C:\\Windows\\Fonts\\times.ttf"])) serif = "Times New Roman";
  else if (anyExists(["/System/Library/Fonts/Supplemental/Times New Roman.ttf"])) {
    serif = "Times New Roman";
  } else if (
    anyExists([
      "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
      "/usr/share/fonts/TTF/DejaVuSerif.ttf",
    ])
  ) {
    serif = "DejaVu Serif";
  } else if (
    anyExists([
      "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationSerif-Regular.ttf",
    ])
  ) {
    serif = "Liberation Serif";
  }
  if (serif === "") return undefined;
  return { sans, serif };
}

const fonts = detectFonts();

/** A two-row page with the same text at the same size, styled by `#a` / `#b`. */
function sampleHtml(css: string): string {
  return `<html><head><style>
    body { margin: 0; }
    .sample { display: inline-block; white-space: nowrap; font-size: 40px; }
    ${css}
  </style></head><body>
    <div id="a" class="sample">mmmmmmmmmm</div>
    <div id="b" class="sample">mmmmmmmmmm</div>
  </body></html>`;
}

function measure(source: string): { a: number; b: number } {
  const { value } = harness.compileAndRun("fonts", source, {
    autocloseMs: 4000,
  });
  return { a: Number(value("a")), b: Number(value("b")) };
}

describe.skipIf(!guiAvailable)("gui extension — real font selection", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it.skipIf(fonts === undefined)("uses the bold face for font-weight: 700", () => {
    const fixture = fonts as FontFixture;
    const html = sampleHtml(`
      #a { font-family: "${fixture.sans}"; font-weight: 400; }
      #b { font-family: "${fixture.sans}"; font-weight: 700; }
    `);
    const { a, b } = measure(`
      import { createWindow, run } from "gui";
      const HTML = ${JSON.stringify(html)};
      const win = createWindow({ title: "font-weight", width: 900, height: 300 });
      win.on("ready", () => {
        console.log("a=" + win.getBoundingClientRect("#a").width);
        console.log("b=" + win.getBoundingClientRect("#b").width);
        win.close();
      });
      win.loadHTML(HTML);
      run();
    `);
    expect(a).toBeGreaterThan(0);
    // The host fixture has a real bold face and its advances are wider here.
    expect(b).toBeGreaterThan(a);
  });

  it.skipIf(fonts === undefined)("uses the requested font-family", () => {
    const fixture = fonts as FontFixture;
    const html = sampleHtml(`
      #a { font-family: "${fixture.sans}"; }
      #b { font-family: "${fixture.serif}"; }
    `);
    const { a, b } = measure(`
      import { createWindow, run } from "gui";
      const HTML = ${JSON.stringify(html)};
      const win = createWindow({ title: "font-family", width: 900, height: 300 });
      win.on("ready", () => {
        console.log("a=" + win.getBoundingClientRect("#a").width);
        console.log("b=" + win.getBoundingClientRect("#b").width);
        win.close();
      });
      win.loadHTML(HTML);
      run();
    `);
    expect(a).toBeGreaterThan(0);
    expect(b).toBeGreaterThan(0);
    expect(Math.abs(a - b)).toBeGreaterThan(1);
  });

  it.skipIf(fonts === undefined)("falls through a missing family to the next one", () => {
    const fixture = fonts as FontFixture;
    const html = sampleHtml(`
      #a { font-family: "${fixture.sans}"; }
      #b { font-family: "No Such Installed Family", "${fixture.sans}"; }
    `);
    const { a, b } = measure(`
      import { createWindow, run } from "gui";
      const HTML = ${JSON.stringify(html)};
      const win = createWindow({ title: "font-family-fallback", width: 900, height: 300 });
      win.on("ready", () => {
        console.log("a=" + win.getBoundingClientRect("#a").width);
        console.log("b=" + win.getBoundingClientRect("#b").width);
        win.close();
      });
      win.loadHTML(HTML);
      run();
    `);
    expect(a).toBeGreaterThan(0);
    expect(b).toBeCloseTo(a, 3);
  });
});
