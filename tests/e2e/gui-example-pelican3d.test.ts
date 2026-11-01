/**
 * End-to-end smoke test for `examples/gui/pelican-3d`.
 *
 * The example is deliberately split across nine ES modules, so this test also
 * proves the gui `.html` loader's deferred module graph still works when a
 * `<script type="module">` imports a whole scene. The native run reports its
 * WebGL 2 version, draw/triangle counts and pixel probes; the browser side is
 * checked statically (the example is a normal module graph there).
 */

import { cpSync, existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
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
  "pelican-3d",
);

/** The Windows gui engine's known intermittent auto-close access violation. */
const ACCESS_VIOLATION = 3221225477;

describe("pelican 3d example — module entry", () => {
  it("loads the scene as a real ES module and keeps the code split", () => {
    const html = readTextFile(join(exampleDir, "index.html"));
    expect(html).toContain('<script type="module" src="./js/main.js"></script>');

    const modules = [
      "main.js", "math.js", "geometry.js", "shaders.js",
      "renderer.js", "world.js", "bike.js", "pelican.js", "audio.js",
    ];
    for (const module of modules) {
      expect(existsSync(join(exampleDir, "js", module)), module).toBe(true);
    }

    const main = readTextFile(join(exampleDir, "js", "main.js"));
    expect(main).toContain('from "./renderer.js"');
    expect(main).toContain('from "./pelican.js"');
    expect(main).toContain('from "./audio.js"');
    expect(main).toContain('getContext("webgl2"');

    const audio = readTextFile(join(exampleDir, "js", "audio.js"));
    expect(audio).toContain("window.AudioContext");
    expect(audio).toContain("createOscillator");
    expect(audio).toContain("createBuffer");
  });
});

describe.skipIf(!guiAvailable)("gui example — pelican 3d", () => {
  let workdir = "";

  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-pelican3d-"));
    cpSync(join(exampleDir, "index.html"), join(workdir, "index.html"));
    cpSync(join(exampleDir, "main.ts"), join(workdir, "main.ts"));
    cpSync(join(exampleDir, "js"), join(workdir, "js"), { recursive: true });
  });

  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  it("renders the WebGL 2 scene and reports pixels", () => {
    const extensions = createDefaultRegistry().register(guiExtension);
    const result = build(join(workdir, "main.ts"), {
      emit: "exe",
      outDir: join(workdir, "out"),
      cacheDir: join(workdir, ".cache"),
      extensions,
      app: { console: true },
    });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);

    const executed = spawnSync(result.outputPath, [], {
      encoding: "utf8",
      env: { ...process.env, XT_GUI_AUTOCLOSE_MS: "8000" },
    });
    const stdout = executed.stdout ?? "";
    const crashed = executed.status === ACCESS_VIOLATION;
    expect(
      executed.status === 0 || crashed,
      `unexpected exit ${executed.status}\nstdout:\n${stdout}\nstderr:\n${executed.stderr}`,
    ).toBe(true);

    const value = (label: string): string => {
      const line = stdout.split(/\r?\n/).find((entry) => entry.startsWith(`${label}=`));
      expect(line, `missing ${label} in:\n${stdout}\n${executed.stderr}`).toBeDefined();
      return line!.slice(label.length + 1).trim();
    };

    expect(value("webgl2")).toBe("true");
    expect(value("gl-version")).toContain("WebGL 2.0");
    expect(value("canvas")).toBe("1100x700");
    expect(value("modules")).toBe("9");
    expect(Number(value("frames"))).toBeGreaterThanOrEqual(12);
    expect(Number(value("fps"))).toBeGreaterThan(0);
    expect(Number(value("draws"))).toBeGreaterThan(30);
    expect(Number(value("triangles"))).toBeGreaterThan(10000);
    expect(value("sound")).toBe("on");
    expect(value("error")).toBe("0");

    const max = value("pixel-max").split(",").map(Number);
    expect(max[0]! + max[1]! + max[2]!).toBeGreaterThan(120);
    expect(max[3]).toBe(255);
  });
});
