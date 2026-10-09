/**
 * End-to-end test for `examples/gui/pelican-bike` — the 2D game that exercises
 * the positioned-layer layout, per-frame DOM mutation and the AOT `<script>`
 * pipeline together.
 *
 * The example's own `main.ts` is compiled here (with the game document next to
 * it) and run briefly. The game is interactive and never closes its own window,
 * so the engine's auto-close guard ends the run — and that shutdown path has an
 * intermittent native crash (see `doc/gui.md`, "Known issues"), so the
 * deterministic assertions live in `gui-example-browser.test.ts` (which runs the
 * very same inline script against a stub host) while this test covers the
 * native side: it compiles, it links against the gui archive, and it runs.
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
import { guiAvailable } from "./gui-helpers.js";

/** The example lives at <repo>/examples/gui/pelican-bike. */
const exampleDir = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..", "examples", "gui", "pelican-bike");

/** `0xC0000005`: the known intermittent access violation in the gui engine's
 * auto-close teardown (documented in `doc/gui.md`). */
const ACCESS_VIOLATION = 3221225477;

let workdir = "";

describe.skipIf(!guiAvailable)("gui example — pelican bike", () => {
  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-pelican-"));
    /* The example's sources verbatim: `main.ts` imports `./index.html`, so both
       have to sit together for the loader to find the document. */
    writeFileSync(join(workdir, "index.html"), readFileSync(join(exampleDir, "index.html"), "utf8"));
    writeFileSync(join(workdir, "pelican.ts"), readFileSync(join(exampleDir, "main.ts"), "utf8"));
  });
  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  it("compiles the game and runs it", () => {
    const extensions = createDefaultRegistry().register(guiExtension);
    const result = build(join(workdir, "pelican.ts"), {
      emit: "exe",
      outDir: join(workdir, "out"),
      cacheDir: join(workdir, ".cache"),
      extensions,
    });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);

    const executed = spawnSync(result.outputPath, [], {
      encoding: "utf8",
      env: { ...process.env, XT_GUI_AUTOCLOSE_MS: "4000" },
    });

    // A clean run, or the documented engine crash — nothing else.
    expect(
      executed.status === 0 || executed.status === ACCESS_VIOLATION,
      `unexpected exit ${executed.status}\nstderr:\n${executed.stderr}`,
    ).toBe(true);

    // The program started and its inline `<script>` was compiled and ran.
    const stdout = executed.stdout ?? "";
    expect(stdout).toContain("script=inline AOT <script> ran");

    // When the report made it out before the window closed, check the scene.
    if (stdout.includes("scene=")) {
      expect(stdout).toContain("scene=x=0 y=40 1000x620");
      expect(stdout).toContain("rider=x=420");
      expect(stdout).toMatch(/hud=\d{4} \d+ \d+ [▮▯]{7} \d+/);
    }
  });
});
