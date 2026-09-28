/**
 * Shared helpers for the `gui` extension e2e suites.
 *
 * Each suite compiles TypeScript programs that import `gui`, links them against
 * the prebuilt `runtime/lib/<os>-<arch>/gui.a` and runs them. Programs close
 * themselves through the `XT_GUI_AUTOCLOSE_MS` hook, so the tests never block on
 * a real window. The suites are skipped when the GUI archive has not been built
 * (run `npm run gui` first) and on Windows, where the archive is not produced
 * yet.
 *
 * `createGuiHarness()` owns one temp directory per suite plus the shared 4x4
 * test PNG; the suite calls `setup()` in `beforeAll` and `cleanup()` in
 * `afterAll`.
 */

import { existsSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { expect } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { guiExtension } from "../../src/extensions/gui/index.js";
import { findRuntimeDir, platformSlug } from "../../src/driver/paths.js";

const archivePath = join(findRuntimeDir(), "lib", platformSlug(), "gui.a");

/** True when the GUI archive exists and we are not on Windows. */
export const guiAvailable = existsSync(archivePath) && process.platform !== "win32";

export interface GuiRunResult {
  stdout: string;
  status: number | null;
  /** Read a `label=value` line out of `stdout`, asserting the label exists. */
  value(label: string): string;
}

export interface GuiHarness {
  /** Absolute path of the 4x4 test PNG written during `setup()`. */
  readonly logoPath: string;
  setup(): void;
  cleanup(): void;
  /** Compile a program and run it with the auto-close hook, returning stdout. */
  compileAndRun(
    name: string,
    source: string,
    extraFiles?: Record<string, string>,
  ): GuiRunResult;
}

/** A 4x4 PNG: left half red, right half blue (base64-encoded inline). */
const LOGO_PNG_BASE64 =
  "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+AAAAFElEQVR42mP4z8DwH4Sh1H8G0gUALFAf4eNWqTEAAAAASUVORK5CYII=";

export function createGuiHarness(): GuiHarness {
  let workdir = "";
  let logoPath = "";

  const compileAndRun = (
    name: string,
    source: string,
    extraFiles: Record<string, string> = {},
  ): GuiRunResult => {
    for (const [fileName, contents] of Object.entries(extraFiles)) {
      writeFileSync(join(workdir, fileName), contents);
    }
    const entry = join(workdir, `${name}.ts`);
    writeFileSync(entry, source);
    const extensions = createDefaultRegistry().register(guiExtension);
    const result = build(entry, {
      emit: "exe",
      outDir: join(workdir, `out-${name}`),
      cacheDir: join(workdir, `.cache-${name}`),
      extensions,
    });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);
    const executed = spawnSync(result.outputPath, [], {
      encoding: "utf8",
      env: { ...process.env, XT_GUI_AUTOCLOSE_MS: "400" },
    });
    expect(executed.status, executed.stderr).toBe(0);
    const stdout = executed.stdout;
    const value = (label: string): string => {
      const line = stdout.split("\n").find((entry) => entry.startsWith(`${label}=`));
      expect(line, `missing ${label} in:\n${stdout}`).toBeDefined();
      return line!.slice(label.length + 1).trim();
    };
    return { stdout, status: executed.status, value };
  };

  return {
    get logoPath() {
      return logoPath;
    },
    setup() {
      workdir = mkdtempSync(join(tmpdir(), "xbintsc-gui-"));
      logoPath = join(workdir, "logo.png");
      writeFileSync(logoPath, Buffer.from(LOGO_PNG_BASE64, "base64"));
    },
    cleanup() {
      rmSync(workdir, { recursive: true, force: true });
    },
    compileAndRun,
  };
}
