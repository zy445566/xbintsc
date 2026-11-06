/**
 * End-to-end smoke test for `examples/gui/webgpu-triangle`.
 *
 * The example renders one WGSL triangle into a `<canvas>` through
 * `navigator.gpu`, then reads the centre pixel back with
 * `copyTextureToBuffer`. A pass proves the example's WGSL, pipeline, bind
 * group, canvas compositing and readback paths all work on this host.
 */

import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
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
  "webgpu-triangle",
);

let workdir = "";

describe.skipIf(!guiAvailable)("gui example — WebGPU triangle", () => {
  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-webgpu-triangle-"));
    for (const file of ["index.html", "main.ts"]) {
      writeFileSync(join(workdir, file), readTextFile(join(exampleDir, file)));
    }
  });
  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  it("renders the WGSL triangle and reports the centre pixel", () => {
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
      env: { ...process.env, XT_GUI_AUTOCLOSE_MS: "12000" },
    });
    expect(executed.status, executed.stderr).toBe(0);
    const stdout = executed.stdout ?? "";
    const value = (label: string): string => {
      const line = stdout.split(/\r?\n/).find((entry) => entry.startsWith(`${label}=`));
      expect(line, `missing ${label} in:\n${stdout}\n${executed.stderr}`).toBeDefined();
      return line!.slice(label.length + 1).trim();
    };

    expect(value("webgpu")).toBe("true");
    expect(value("format")).toBe("bgra8unorm");
    expect(value("messages")).toBe("0");
    expect(value("center")).toMatch(/^[0-9]+,[0-9]+,[0-9]+,255$/);
    expect(value("ok")).toBe("true");
  });
});
