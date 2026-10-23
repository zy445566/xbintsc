/**
 * End-to-end smoke test for `examples/gui/pelican-canvas` — the animated
 * `<canvas>` drawing.
 *
 * The drawing lives in `scene.js`, loaded by `index.html` through a plain
 * `<script src>`: a browser runs it as-is, while xbintsc AOT-compiles the same
 * body via the gui `.html` loader. `main.ts` only hosts the native window and
 * reports deterministic pixel probes, so this test can assert that the
 * gradients, paths and text really rasterised.
 */

import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { Script } from "node:vm";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { guiExtension } from "../../src/extensions/gui/index.js";
import { guiAvailable } from "./gui-helpers.js";

/** The example lives at <repo>/examples/gui/pelican-canvas. */
const exampleDir = resolve(
  dirname(fileURLToPath(import.meta.url)),
  "..",
  "..",
  "examples",
  "gui",
  "pelican-canvas",
);

let workdir = "";

describe("pelican canvas example — browser entry", () => {
  it("loads the shared scene as a classic script", () => {
    const html = readFileSync(join(exampleDir, "index.html"), "utf8");
    expect(html).toContain('<script src="./scene.js"></script>');
    const script = readFileSync(join(exampleDir, "scene.js"), "utf8");
    // A browser compiles this as a classic script: no ESM, no TypeScript.
    expect(() => new Script(script)).not.toThrow();
  });
});

describe.skipIf(!guiAvailable)("gui example — pelican canvas", () => {
  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-pelican-canvas-"));
    for (const file of ["index.html", "main.ts", "scene.js"]) {
      writeFileSync(join(workdir, file), readFileSync(join(exampleDir, file), "utf8"));
    }
  });
  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  it("compiles the canvas drawing and reports pixels", () => {
    const extensions = createDefaultRegistry().register(guiExtension);
    const result = build(join(workdir, "main.ts"), {
      emit: "exe",
      outDir: join(workdir, "out"),
      cacheDir: join(workdir, ".cache"),
      extensions,
    });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);

    const executed = spawnSync(result.outputPath, [], {
      encoding: "utf8",
      env: { ...process.env, XT_GUI_AUTOCLOSE_MS: "2000" },
    });
    expect(executed.status, executed.stderr).toBe(0);
    const stdout = executed.stdout ?? "";

    expect(stdout).toContain("window=1000x640 (gui extension, SDL_GPU)");
    expect(stdout).toContain("canvas-box=x=0 y=0 1000x640");
    expect(stdout).toContain("canvas-size=1000x640");
    // The title is white text on the gradient sky.
    expect(stdout).toContain("pixel-title=255,255,255,255");
    // The road gradient, and the pelican body (not the sky colour).
    expect(stdout).toMatch(/pixel-road=\d+,\d+,\d+,255/);
    expect(stdout).toMatch(/pixel-pelican=\d+,\d+,\d+,255/);
    expect(stdout).toContain("gui exited");
  });
});
