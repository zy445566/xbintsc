/**
 * GUI e2e: author `@font-face` rules load real local font files.
 *
 * The program is spawned with the suite's work directory as CWD, so a relative
 * `src: url("./fonts/…")` exercises the documented `file:` resolution. The same
 * measurements run once with the default (auto-selected) shaping backend and
 * once with `--no-feature shaping`, because custom files may be TrueType (both
 * backends) or CFF (shaping only).
 */

import { copyFileSync, existsSync, mkdirSync } from "node:fs";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

interface CustomFonts {
  regular: string;
  bold: string;
}

/** A serif fixture whose regular and bold faces are visibly different. */
function detectCustomFonts(): CustomFonts | undefined {
  const pairs: readonly (readonly [string, string])[] = [
    [
      "/System/Library/Fonts/Supplemental/Georgia.ttf",
      "/System/Library/Fonts/Supplemental/Georgia Bold.ttf",
    ],
    [
      "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
    ],
    [
      "/usr/share/fonts/TTF/DejaVuSerif.ttf",
      "/usr/share/fonts/TTF/DejaVuSerif-Bold.ttf",
    ],
    [
      "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf",
    ],
    ["C:\\Windows\\Fonts\\georgia.ttf", "C:\\Windows\\Fonts\\georgiab.ttf"],
    ["C:\\Windows\\Fonts\\times.ttf", "C:\\Windows\\Fonts\\timesbd.ttf"],
  ];
  for (const [regular, bold] of pairs) {
    if (existsSync(regular) && existsSync(bold)) return { regular, bold };
  }
  return undefined;
}

const customFonts = detectCustomFonts();

function sampleHtml(fileUrl: string): string {
  return `<html><head><style>
    body { margin: 0; }
    .sample { display: inline-block; white-space: nowrap; font-size: 40px; }
    @font-face {
      font-family: "Xt Relative";
      src: local("No Such Local Font"),
           url("./fonts/absent.woff2") format("woff2"),
           url("./fonts/custom-regular.ttf") format("truetype");
      font-weight: 400;
    }
    @font-face {
      font-family: "Xt Relative";
      src: url("./fonts/custom-bold.ttf") format("truetype");
      font-weight: 700;
    }
    @font-face {
      font-family: "Xt File";
      src: url("${fileUrl}");
    }
    @font-face {
      font-family: "Xt Missing";
      src: url("./fonts/does-not-exist.ttf");
    }
    #regular { font-family: "Xt Relative"; font-weight: 400; }
    #bold { font-family: "Xt Relative"; font-weight: 700; }
    #file { font-family: "Xt File"; }
    #missing { font-family: "Xt Missing", sans-serif; }
    #control { font-family: sans-serif; }
  </style></head><body>
    <div id="regular" class="sample">mmmmmmmmmm</div>
    <div id="bold" class="sample">mmmmmmmmmm</div>
    <div id="file" class="sample">mmmmmmmmmm</div>
    <div id="missing" class="sample">mmmmmmmmmm</div>
    <div id="control" class="sample">mmmmmmmmmm</div>
  </body></html>`;
}

interface MeasureOptions {
  name: string;
  features?: readonly string[];
  noFeatures?: readonly string[];
}

function measure(options: MeasureOptions): {
  regular: number;
  bold: number;
  file: number;
  missing: number;
  control: number;
} {
  const fileUrl = pathToFileURL(join(harness.workdir, "fonts", "custom-regular.ttf")).href;
  const html = sampleHtml(fileUrl);
  const { value } = harness.compileAndRun(
    options.name,
    `
    import { createWindow, run } from "gui";
    const HTML = ${JSON.stringify(html)};
    const win = createWindow({ title: "font-face", width: 900, height: 500 });
    win.on("ready", () => {
      console.log("regular=" + win.getBoundingClientRect("#regular").width);
      console.log("bold=" + win.getBoundingClientRect("#bold").width);
      console.log("file=" + win.getBoundingClientRect("#file").width);
      console.log("missing=" + win.getBoundingClientRect("#missing").width);
      console.log("control=" + win.getBoundingClientRect("#control").width);
      win.close();
    });
    win.loadHTML(HTML);
    run();
    `,
    {
      cwd: harness.workdir,
      autocloseMs: 4000,
      ...(options.features !== undefined ? { features: options.features } : {}),
      ...(options.noFeatures !== undefined ? { noFeatures: options.noFeatures } : {}),
    },
  );
  return {
    regular: Number(value("regular")),
    bold: Number(value("bold")),
    file: Number(value("file")),
    missing: Number(value("missing")),
    control: Number(value("control")),
  };
}

function expectCustomFonts(result: ReturnType<typeof measure>): void {
  expect(result.regular).toBeGreaterThan(0);
  // The custom family is a serif fixture; if @font-face had not resolved, the
  // element would have measured exactly like the generic sans control.
  expect(Math.abs(result.regular - result.control)).toBeGreaterThan(1);
  // The 700 descriptor selects the bold file rather than reusing the 400 face.
  expect(result.bold).toBeGreaterThan(result.regular);
  // An absolute `file:` URL resolves to the same regular face.
  expect(result.file).toBeCloseTo(result.regular, 3);
  // A family whose only source is missing falls through to the next family.
  expect(result.missing).toBeCloseTo(result.control, 3);
}

describe.skipIf(!guiAvailable)("gui extension — author @font-face", () => {
  beforeAll(() => {
    harness.setup();
    if (customFonts !== undefined) {
      mkdirSync(join(harness.workdir, "fonts"), { recursive: true });
      copyFileSync(customFonts.regular, join(harness.workdir, "fonts", "custom-regular.ttf"));
      copyFileSync(customFonts.bold, join(harness.workdir, "fonts", "custom-bold.ttf"));
    }
  });
  afterAll(() => harness.cleanup());

  it.skipIf(customFonts === undefined)("loads relative and file: sources through stb_truetype", () => {
    expectCustomFonts(measure({ name: "font-face-stb", noFeatures: ["shaping"] }));
  });

  it.skipIf(customFonts === undefined)("loads the same sources through the shaping backend", () => {
    expectCustomFonts(measure({ name: "font-face-shaping", features: ["shaping"] }));
  });
});
