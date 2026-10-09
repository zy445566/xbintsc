/**
 * Shared helpers for the `gui` extension e2e suites.
 *
 * Each suite compiles TypeScript programs that import `gui`, links them against
 * the prebuilt `runtime/lib/<os>-<arch>/gui.a` and runs them. Programs close
 * themselves through the `XT_GUI_AUTOCLOSE_MS` hook, so the tests never block on
 * a real window. The suites are skipped when the GUI archive has not been built
 * (run `npm run gui` first) and on Windows by default, where the archive is not
 * validated in CI yet — `xbintsc_GUI_TESTS=1` opts in there.
 *
 * `XT_GUI_AUTOCLOSE_MS` is a *hang guard*, not a test budget: it is the wall
 * clock the frame loop is allowed to run before it gives up and exits. A program
 * that closes its own window ends the loop on its own, so a guard that is too
 * tight only breaks tests that need several frames — raise it with
 * `GuiRunOptions.autocloseMs` for those.
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

/**
 * True when the GUI archive exists and the suite may run.
 *
 * Windows is skipped by default: the archive (`gui.lib`) is produced by
 * `scripts/build-gui.ts`, but CI has not validated the GUI there. Set
 * `xbintsc_GUI_TESTS=1` to opt in on a Windows developer machine that has one —
 * the suites open a real window, so they need a working GPU/display.
 */
export const guiAvailable =
  (existsSync(archivePath) || existsSync(join(findRuntimeDir(), "lib", platformSlug(), "gui.lib"))) &&
  (process.platform !== "win32" || process.env.xbintsc_GUI_TESTS === "1");

export interface GuiRunResult {
  stdout: string;
  stderr: string;
  status: number | null;
  /** Read a `label=value` line out of `stdout`, asserting the label exists. */
  value(label: string): string;
}

export interface GuiRunOptions {
  /** Extra files written into the work directory before the build. */
  files?: Record<string, string>;
  /**
   * Milliseconds the frame loop may run before `XT_GUI_AUTOCLOSE_MS` closes
   * every window. A program whose handlers close the window does not need this
   * at all — it is only the ceiling for a program that would otherwise hang, so
   * raising it costs nothing when the test passes. Programs that need more than
   * one rendered frame must raise it: the first frame does the one-time GPU work
   * (shader and pipeline creation, the font atlas), which a software Vulkan
   * driver on a loaded CI runner can stretch far past the default.
   */
  autocloseMs?: number;
}

/** Enough for programs whose whole body runs in the first frame's `ready` handler. */
const DEFAULT_AUTOCLOSE_MS = 400;

export interface GuiHarness {
  /** Absolute path of the 4x4 test PNG written during `setup()`. */
  readonly logoPath: string;
  /**
   * The same path, safe to embed in HTML/CSS. On Windows an absolute path
   * contains backslashes, which an HTML attribute (or a CSS `url()`) would eat,
   * so it is written with `/` separators and percent-encoded on the way in —
   * the engine decodes it again (`xt_image_load`).
   */
  imageSrc(absolutePath: string): string;
  setup(): void;
  cleanup(): void;
  /** Compile a program and run it with the auto-close hook, returning stdout. */
  compileAndRun(name: string, source: string, options?: GuiRunOptions): GuiRunResult;
}

/** A 4x4 PNG: left half red, right half blue (base64-encoded inline). */
const LOGO_PNG_BASE64 =
  "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+AAAAFElEQVR42mP4z8DwH4Sh1H8G0gUALFAf4eNWqTEAAAAASUVORK5CYII=";

/**
 * Make an absolute path embeddable in an HTML attribute or CSS `url()`: use `/`
 * separators (valid on Windows too) and percent-encode the rest, which is how
 * the engine's image loader expects a local path to arrive.
 */
function toImageSrc(absolutePath: string): string {
  return encodeURI(absolutePath.split("\\").join("/"));
}

export function createGuiHarness(): GuiHarness {
  let workdir = "";
  let logoPath = "";

  const compileAndRun = (
    name: string,
    source: string,
    options: GuiRunOptions = {},
  ): GuiRunResult => {
    for (const [fileName, contents] of Object.entries(options.files ?? {})) {
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
      env: {
        ...process.env,
        XT_GUI_AUTOCLOSE_MS: String(options.autocloseMs ?? DEFAULT_AUTOCLOSE_MS),
      },
    });
    expect(executed.status, executed.stderr).toBe(0);
    const stdout = executed.stdout;
    const stderr = executed.stderr;
    const value = (label: string): string => {
      const line = stdout.split("\n").find((entry) => entry.startsWith(`${label}=`));
      expect(line, `missing ${label} in stdout:\n${stdout}\nstderr:\n${stderr}`).toBeDefined();
      return line!.slice(label.length + 1).trim();
    };
    return { stdout, stderr, status: executed.status, value };
  };

  return {
    get logoPath() {
      return logoPath;
    },
    imageSrc: toImageSrc,
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
