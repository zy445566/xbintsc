/**
 * End-to-end smoke test for `examples/gui/pelican-3d`.
 *
 * The example is deliberately split across ES modules, so this test also
 * proves the gui `.html` loader's deferred module graph still works when a
 * `<script type="module">` imports a whole scene. It runs the same scene twice:
 * once in the default WebGL 2 backend and once with `DEFAULT_BACKEND` flipped
 * to WebGPU, checking the native report's backend, draw/triangle counts and
 * pixel probes in both. The browser side is checked statically (the example is
 * a normal module graph there).
 */

import { cpSync, existsSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
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

interface ExampleRun {
  value(label: string): string;
}

/** Copy the example to a temp directory, optionally forcing a backend. */
function prepareWorkdir(backend: "webgl2" | "webgpu"): string {
  const workdir = mkdtempSync(join(tmpdir(), "xbintsc-pelican3d-"));
  cpSync(join(exampleDir, "index.html"), join(workdir, "index.html"));
  cpSync(join(exampleDir, "main.ts"), join(workdir, "main.ts"));
  cpSync(join(exampleDir, "js"), join(workdir, "js"), { recursive: true });
  if (backend === "webgpu") {
    const entry = join(workdir, "js", "main.js");
    const source = readTextFile(entry).replace(
      'const DEFAULT_BACKEND = "webgl2";',
      'const DEFAULT_BACKEND = "webgpu";',
    );
    writeFileSync(entry, source);
  }
  return workdir;
}

/** Build and run one configuration, returning the `label=value` report. */
function runExample(workdir: string, autocloseMs: number): ExampleRun {
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
    env: { ...process.env, XT_GUI_AUTOCLOSE_MS: String(autocloseMs) },
  });
  const stdout = executed.stdout ?? "";
  const crashed = executed.status === ACCESS_VIOLATION;
  expect(
    executed.status === 0 || crashed,
    `unexpected exit ${executed.status}\nstdout:\n${stdout}\nstderr:\n${executed.stderr}`,
  ).toBe(true);

  return {
    value(label: string): string {
      const line = stdout.split(/\r?\n/).find((entry) => entry.startsWith(`${label}=`));
      expect(line, `missing ${label} in:\n${stdout}\n${executed.stderr}`).toBeDefined();
      return line!.slice(label.length + 1).trim();
    },
  };
}

describe("pelican 3d example — module entry", () => {
  it("loads the scene as a real ES module and keeps the code split", () => {
    const html = readTextFile(join(exampleDir, "index.html"));
    expect(html).toContain('<script type="module" src="./js/main.js"></script>');
    expect(html).toContain('id="scene-webgl2"');
    expect(html).toContain('id="scene-webgpu"');
    expect(html).toContain('id="backend"');

    const modules = [
      "main.js", "math.js", "geometry.js", "shaders.js", "shaders-wgsl.js",
      "renderer.js", "renderer-webgl.js", "renderer-webgpu.js",
      "world.js", "bike.js", "pelican.js", "audio.js",
    ];
    for (const module of modules) {
      expect(existsSync(join(exampleDir, "js", module)), module).toBe(true);
    }

    const main = readTextFile(join(exampleDir, "js", "main.js"));
    expect(main).toContain('from "./renderer.js"');
    expect(main).toContain('from "./pelican.js"');
    expect(main).toContain('from "./audio.js"');
    expect(main).toContain("DEFAULT_BACKEND");

    const renderer = readTextFile(join(exampleDir, "js", "renderer.js"));
    expect(renderer).toContain('getContext("webgl2"');
    expect(renderer).toContain('from "./renderer-webgpu.js"');
    expect(readTextFile(join(exampleDir, "js", "shaders-wgsl.js"))).toContain("@vertex");

    const audio = readTextFile(join(exampleDir, "js", "audio.js"));
    expect(audio).toContain("window.AudioContext");
    expect(audio).toContain("createOscillator");
    expect(audio).toContain("createBuffer");
  });
});

describe.skipIf(!guiAvailable)("gui example — pelican 3d", () => {
  let workdir = "";

  beforeAll(() => {
    workdir = prepareWorkdir("webgl2");
  });

  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  it("renders the WebGL 2 scene and reports pixels", () => {
    const run = runExample(workdir, 10000);

    expect(run.value("backend")).toBe("webgl2");
    expect(run.value("webgl2")).toBe("true");
    expect(run.value("webgpu")).toBe("false");
    expect(run.value("renderer")).toBe("WEBGL 2");
    expect(run.value("gl-version")).toContain("WebGL 2.0");
    expect(run.value("canvas")).toBe("1100x700");
    expect(run.value("canvas-composite")).toBe("true");
    expect(run.value("modules")).toBe("12");
    expect(Number(run.value("frames"))).toBeGreaterThanOrEqual(12);
    expect(Number(run.value("fps"))).toBeGreaterThan(0);
    expect(Number(run.value("draws"))).toBeGreaterThan(30);
    expect(Number(run.value("triangles"))).toBeGreaterThan(10000);
    expect(run.value("sound")).toBe("on");
    expect(run.value("error")).toBe("0");

    const max = run.value("pixel-max").split(",").map(Number);
    expect(max[0]! + max[1]! + max[2]!).toBeGreaterThan(120);
    expect(max[3]).toBe(255);
  });
});

describe.skipIf(!guiAvailable)("gui example — pelican 3d WebGPU", () => {
  let workdir = "";

  beforeAll(() => {
    workdir = prepareWorkdir("webgpu");
  });

  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  it("renders the same scene through WebGPU and reports pixels", () => {
    const run = runExample(workdir, 15000);

    expect(run.value("backend")).toBe("webgpu");
    expect(run.value("webgl2")).toBe("false");
    expect(run.value("webgpu")).toBe("true");
    expect(run.value("renderer")).toBe("WEBGPU");
    expect(run.value("gl-version")).toBe("WebGPU (WGSL)");
    expect(run.value("canvas")).toBe("1100x700");
    expect(run.value("canvas-composite")).toBe("true");
    expect(run.value("modules")).toBe("12");
    expect(Number(run.value("frames"))).toBeGreaterThanOrEqual(12);
    expect(Number(run.value("fps"))).toBeGreaterThan(0);
    expect(Number(run.value("draws"))).toBeGreaterThan(30);
    expect(Number(run.value("triangles"))).toBeGreaterThan(10000);
    expect(run.value("sound")).toBe("on");
    expect(run.value("error")).toBe("0");

    const max = run.value("pixel-max").split(",").map(Number);
    expect(max[0]! + max[1]! + max[2]!).toBeGreaterThan(120);
    expect(max[3]).toBe(255);
  });
});
