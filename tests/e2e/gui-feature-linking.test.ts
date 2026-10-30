/**
 * End-to-end proof that optional link features really are symbol-level: a
 * DOM-only GUI program must not contain FFmpeg or glslang, while a WebGL or
 * media program must.
 *
 * These tests only *build*; they never open a window, so they do not need a GPU.
 * They still need the prebuilt `gui.a`/`gui.lib`, hence `guiAvailable`.
 */

import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { findRuntimeLibrary } from "../../src/driver/runtime-lib.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { guiExtension } from "../../src/extensions/gui/index.js";
import { guiAvailable } from "./gui-helpers.js";

/** Build one program against `gui` and return its binary. */
function buildBinary(name: string, source: string): Buffer {
  const directory = mkdtempSync(join(tmpdir(), `xbintsc-link-${name}-`));
  try {
    const entry = join(directory, "app.ts");
    writeFileSync(entry, source);
    const result = build(entry, {
      emit: "exe",
      outDir: join(directory, "out"),
      cacheDir: join(directory, ".cache"),
      extensions: createDefaultRegistry().register(guiExtension),
    });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);
    return readFileSync(result.outputPath);
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
}

/** Whether the binary contains an ASCII marker from a statically linked library. */
function contains(binary: Buffer, marker: string): boolean {
  return binary.includes(Buffer.from(marker));
}

const WINDOW = `import { createWindow } from "gui";
const win = createWindow({ title: "link features", width: 120, height: 80 });
`;

describe.skipIf(!guiAvailable)("link features are selected per program", () => {
  it("leaves FFmpeg and glslang out of a DOM-only program", () => {
    const binary = buildBinary("dom", `${WINDOW}win.loadHTML("<div>hello</div>");`);
    expect(contains(binary, "glslang Khronos")).toBe(false);
    expect(contains(binary, "matroska")).toBe(false);
  });

  it("links glslang when the program may create a WebGL context", () => {
    const binary = buildBinary(
      "webgl",
      `${WINDOW}const canvas: any = win.document.createElement("canvas");
canvas.getContext("webgl2");
`,
    );
    expect(contains(binary, "glslang Khronos")).toBe(true);
  });

  it("links FFmpeg when the program may play a media element", () => {
    if (findRuntimeLibrary("media") === undefined) return;
    const binary = buildBinary("media", `${WINDOW}win.loadHTML("<video src='x.mp4'></video>");`);
    expect(contains(binary, "matroska")).toBe(true);
  });
});
