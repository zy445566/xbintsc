/**
 * End-to-end smoke test for `examples/gui/webgl-cube`.
 *
 * The scene (`scene.js`) is loaded by `index.html` as a classic script, so a
 * browser runs the same document. The test compiles the native host, runs it
 * with an auto-close guard, and checks the readback probes: the cube's faces
 * are lit by the directional light and the whole frame is produced by WebGL on
 * the GPU.
 */

import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
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
import { readTextFile } from "../platform.js";

const exampleDir = resolve(
  dirname(fileURLToPath(import.meta.url)),
  "..",
  "..",
  "examples",
  "gui",
  "webgl-cube",
);

let workdir = "";

describe("webgl cube example — browser entry", () => {
  it("loads the shared scene as a classic script", () => {
    const html = readTextFile(join(exampleDir, "index.html"));
    expect(html).toContain('<script src="./scene.js"></script>');
    const script = readTextFile(join(exampleDir, "scene.js"));
    expect(() => new Script(script)).not.toThrow();
  });
});

describe.skipIf(!guiAvailable)("gui example — WebGL cube", () => {
  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-webgl-cube-"));
    for (const file of ["index.html", "main.ts", "scene.js"]) {
      writeFileSync(join(workdir, file), readTextFile(join(exampleDir, file)));
    }
  });
  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  it("renders the lit cube and reports pixels", () => {
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
      env: { ...process.env, XT_GUI_AUTOCLOSE_MS: "6000" },
    });
    expect(executed.status, executed.stderr).toBe(0);
    const stdout = executed.stdout ?? "";
    const value = (label: string): string => {
      const line = stdout.split(/\r?\n/).find((entry) => entry.startsWith(`${label}=`));
      expect(line, `missing ${label} in:\n${stdout}\n${executed.stderr}`).toBeDefined();
      return line!.slice(label.length + 1).trim();
    };

    expect(value("window")).toBe("800x640 (gui extension, SDL_GPU)");
    expect(value("gpu").length).toBeGreaterThan(0);
    expect(value("webgl")).toBe("true");
    expect(value("canvas")).toBe("800x640");
    expect(value("error")).toBe("0");
    expect(Number(value("frames"))).toBeGreaterThanOrEqual(3);
    // The brightest pixel across the cube's box: a lit face, not the dark
    // background.
    const max = value("pixel-max").split(",").map(Number);
    expect(max[0]! + max[1]! + max[2]!).toBeGreaterThan(60);
    expect(max[3]).toBe(255);
  });
});
