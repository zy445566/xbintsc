/**
 * End-to-end tests for the `gui` extension.
 *
 * The suite compiles TypeScript programs that import `gui`, links them against
 * the prebuilt `runtime/lib/<os>-<arch>/gui.a` and runs them. Programs close
 * themselves through the `XT_GUI_AUTOCLOSE_MS` hook, so the tests never block on
 * a real window.
 *
 * It is skipped when the GUI archive has not been built (run `npm run gui`
 * first) and on Windows, where the archive is not produced yet.
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

  /** Compile a program and run it with the auto-close hook, returning stdout. */
  function compileAndRun(name: string, source: string): { stdout: string; status: number | null } {
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
    return { stdout: executed.stdout, status: executed.status };
  }

  it("opens a GPU-backed window and delivers lifecycle events", () => {
    const { stdout } = compileAndRun(
      "window",
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
    const lines = stdout.trim().split("\n");
    expect(lines).toContain("gui ready");
    expect(lines).toContain("gui closed");
    expect(lines).toContain("gui exited");
  });

  it("parses HTML/CSS and cascades computed styles", () => {
    const { stdout } = compileAndRun(
      "cascade",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html>
        <head>
          <style>
            body { font-size: 16px; color: #111111; }
            div { width: 10%; }
            .box { width: 25%; }
            #main { width: 60%; color: #222222; }
            .box > .inner { padding-top: 3px; }
            .box .inner { padding-top: 5px; }
            li:first-child { color: #555555; }
            li:nth-child(2) { color: #666666; }
            li:not(.skip) { font-weight: bold; }
            [data-role="lead"] { text-align: center; }
            .em { font-size: 2em; }
            .rem { font-size: 2rem; }
            .imp { color: #000000; }
            .imp { color: #ff0000 !important; }
          </style>
        </head>
        <body>
          <div id="main" class="box">
            <div class="inner" style="color: #00ff00">x</div>
            <div class="inner">y</div>
          </div>
          <ul>
            <li>one</li>
            <li>two</li>
            <li class="skip">three</li>
          </ul>
          <p data-role="lead">lead</p>
          <div class="em">em</div>
          <div class="rem">rem</div>
          <div class="imp" style="color: #0000ff">important</div>
        </body>
      </html>
      \`;

      const win = createWindow({ title: "cascade", width: 800, height: 600 });
      win.on("ready", () => {
        const out = (label, value) => console.log(label + "=" + value);
        out("main-width", win.computedStyle("#main", "width"));
        out("inner-padding", win.computedStyle(".inner", "padding-top"));
        out("inner-color", win.computedStyle(".inner", "color"));
        out("body-color", win.computedStyle("body", "color"));
        out("li1-color", win.computedStyle("li:first-child", "color"));
        out("li2-color", win.computedStyle("li:nth-child(2)", "color"));
        out("li2-weight", win.computedStyle("li:nth-child(2)", "font-weight"));
        out("skip-weight", win.computedStyle("li.skip", "font-weight"));
        out("lead-align", win.computedStyle("[data-role=\\"lead\\"]", "text-align"));
        out("em-size", win.computedStyle(".em", "font-size"));
        out("rem-size", win.computedStyle(".rem", "font-size"));
        out("imp-color", win.computedStyle(".imp", "color"));
        out("li-count", win.queryCount("li"));
        out("lead-count", win.queryCount("[data-role=\\"lead\\"]"));
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    const value = (label: string): string => {
      const line = stdout.split("\n").find((entry) => entry.startsWith(`${label}=`));
      expect(line, `missing ${label} in:\n${stdout}`).toBeDefined();
      return line!.slice(label.length + 1).trim();
    };

    expect(value("main-width")).toBe("60%"); // id beats class and tag
    expect(value("inner-padding")).toBe("5px"); // equal specificity, later rule wins
    expect(value("inner-color")).toBe("rgb(0, 255, 0)"); // inline beats the sheet
    expect(value("body-color")).toBe("rgb(17, 17, 17)");
    expect(value("li1-color")).toBe("rgb(85, 85, 85)"); // :first-child
    expect(value("li2-color")).toBe("rgb(102, 102, 102)"); // :nth-child(2)
    expect(value("li2-weight")).toBe("700"); // :not(.skip)
    expect(value("skip-weight")).toBe("400"); // :not(.skip) excluded it
    expect(value("lead-align")).toBe("center"); // [data-role="lead"]
    expect(value("em-size")).toBe("32px"); // 2em of the 16px parent
    expect(value("rem-size")).toBe("32px"); // 2rem of the 16px root
    expect(value("imp-color")).toBe("rgb(255, 0, 0)"); // !important beats inline
    expect(value("li-count")).toBe("3");
    expect(value("lead-count")).toBe("1");
  });
});
