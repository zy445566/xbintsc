/**
 * End-to-end tests for the `gui` extension.
 *
 * The suite compiles a TypeScript program that imports `gui`, links it against
 * the prebuilt `runtime/lib/<os>-<arch>/gui.a` and runs it. The program closes
 * itself through the `XT_GUI_AUTOCLOSE_MS` hook, so the test never blocks on a
 * real window.
 *
 * It is skipped when the GUI archive has not been built (run
 * `npm run gui` first) and on Windows, where the archive is not produced yet.
 */

import { existsSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { guiExtension } from "../../src/extensions/gui/index.js";
import { findRuntimeDir, platformSlug } from "../../src/driver/paths.js";

const archivePath = join(findRuntimeDir(), "lib", platformSlug(), "gui.a");
const available = existsSync(archivePath) && process.platform !== "win32";

describe.skipIf(!available)("gui extension", () => {
  let workdir: string;
  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-gui-"));
  });
  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  it("opens a GPU-backed window and delivers lifecycle events", () => {
    const entry = join(workdir, "window.ts");
    writeFileSync(
      entry,
      `
      import { createWindow, run } from "gui";
      const win = createWindow({ title: "test window", width: 320, height: 240 });
      win.on("ready", () => console.log("gui ready"));
      win.on("close", () => console.log("gui closed"));
      win.setBackground("#101010");
      win.loadHTML("<h1>hi</h1>");
      run();
      console.log("gui exited");
      `,
    );

    const extensions = createDefaultRegistry().register(guiExtension);
    const result = build(entry, {
      emit: "exe",
      outDir: join(workdir, "out"),
      cacheDir: join(workdir, ".cache"),
      extensions,
    });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);

    const executed = spawnSync(result.outputPath, [], {
      encoding: "utf8",
      env: { ...process.env, XT_GUI_AUTOCLOSE_MS: "400" },
    });
    expect(executed.status, executed.stderr).toBe(0);
    const lines = executed.stdout.trim().split("\n");
    expect(lines).toContain("gui ready");
    expect(lines).toContain("gui closed");
    expect(lines).toContain("gui exited");
  });
});
