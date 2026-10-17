/**
 * End-to-end test for `examples/gui/pelican-svg` — the SVG gallery page that
 * animates a CSS `transform` from the frame loop.
 *
 * The example's own `main.ts` is compiled here together with its `index.html`
 * (the `gui` extension compiles the document and the `<img>` path it points at),
 * linked against the prebuilt `gui.a`, and run under the `XT_GUI_AUTOCLOSE_MS`
 * hang guard. The assertions read the example's `label=value` report, which is
 * printed from the first presented frame:
 *
 *   - the document parsed, the SVG decoded and the image reached the paint list;
 *   - the artwork's *layout* box is the 720x520 box CSS asked for;
 *   - each frame's pose (a `transform` list parsed by the engine) composed into a
 *     matrix that reached the display list, and the reported bounding box moved
 *     with it — a paint-time transform does not touch layout;
 *   - hit testing still lands on the `<img>` through the transform.
 *
 * The pose values themselves are wall-clock dependent (`requestAnimationFrame`
 * timestamps), so the test matches their *shape*, not their numbers.
 */

import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { guiExtension } from "../../src/extensions/gui/index.js";
import { nodeExtension } from "../../src/extensions/node/index.js";
import { guiAvailable } from "./gui-helpers.js";

/** The example lives at <repo>/examples/gui/pelican-svg. */
const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
const exampleDir = join(repoRoot, "examples", "gui", "pelican-svg");

/** Windows reports an access violation as this NTSTATUS exit code; on POSIX the
 * same documented teardown fault surfaces as a signal (see
 * `doc/gui/known-issues.md`). */
const ACCESS_VIOLATION = 3221225477;
const CRASH_SIGNALS = new Set(["SIGSEGV", "SIGBUS", "SIGABRT", "SIGILL", "SIGFPE"]);

let workdir = "";

describe.skipIf(!guiAvailable)("gui example — pelican svg", () => {
  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-pelican-svg-"));
    writeFileSync(join(workdir, "index.html"), readFileSync(join(exampleDir, "index.html"), "utf8"));
    writeFileSync(join(workdir, "pelican-svg.ts"), readFileSync(join(exampleDir, "main.ts"), "utf8"));
  });
  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  it("compiles the gallery and animates the artwork", () => {
    const extensions = createDefaultRegistry().register(guiExtension).register(nodeExtension);
    const result = build(join(workdir, "pelican-svg.ts"), {
      emit: "exe",
      outDir: join(workdir, "out"),
      cacheDir: join(workdir, ".cache"),
      extensions,
    });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);

    /* The example finds the artwork relative to the working directory, so it is
       spawned from the repository root — the same way the CLI runs it. */
    const executed = spawnSync(result.outputPath, [], {
      cwd: repoRoot,
      encoding: "utf8",
      env: { ...process.env, XT_GUI_AUTOCLOSE_MS: "4000" },
    });

    const crashed =
      executed.status === ACCESS_VIOLATION ||
      (executed.status === null && executed.signal !== null && CRASH_SIGNALS.has(executed.signal));
    const outcome = executed.signal !== null ? `signal ${executed.signal}` : `exit ${executed.status}`;
    expect(executed.status === 0 || crashed, `unexpected ${outcome}\nstderr:\n${executed.stderr}`).toBe(
      true,
    );

    const stdout = executed.stdout ?? "";
    const value = (label: string): string => {
      const found = stdout.split("\n").find((entry) => entry.startsWith(`${label}=`));
      expect(found, `missing ${label} in stdout:\n${stdout}\nstderr:\n${executed.stderr}`).toBeDefined();
      return found!.slice(label.length + 1).trim();
    };

    // The window, the backend and the document: the artwork laid out at 720x520
    // and the vector source decoded into a single painted image.
    expect(value("window")).toBe("800x640 (gui extension, SDL_GPU)");
    expect(value("art-box")).toBe("x=40 y=35 720x520");
    expect(value("painted-images")).toBe("1");
    expect(value("painted-image")).toMatch(/^image x=40\.0 y=35\.0 w=720\.0 h=520\.0 src=.*pelican-bike\.svg$/);
    expect(value("svg-declares")).toBe("720x520");
    expect(value("svg-has-viewbox")).toBe("true");
    expect(value("svg-paths")).toBe("44");

    // The SVG is `<img>` content, so it is hit-testable like any other element,
    // and `transform-origin` came through the cascade unresolved (`%`), as CSS
    // requires: layout resolves it against the border box.
    expect(value("hit-test")).toBe("img#art");
    expect(value("art-origin")).toBe("50% 92%");

    // The frame loop applied a pose: a `transform` list the engine parsed into a
    // matrix, which is what the renderer received for the image quad.
    /* Match the *shape* only: the values follow the wall clock, and the engine's
       canonical spelling trims trailing zeros (`rotate(0.8deg)`, not `0.80deg`). */
    const pose = /^translateY\(-?[\d.]+px\) rotate\(-?[\d.]+deg\) scale\([\d.]+, [\d.]+\)$/;
    expect(value("art-pose@1")).toMatch(pose);
    expect(value("art-pose@2")).toMatch(pose);
    expect(value("art-matrix")).toMatch(/^matrix\(/);

    // ...and the geometry the DOM reports follows the transform, while the
    // layout box above stayed at 720x520.
    const moved = value("art-moved-box");
    expect(moved).toMatch(/^x=-?\d+ y=-?\d+ \d+x\d+$/);
    expect(moved).not.toBe("x=40 y=35 720x520");
  });
});
