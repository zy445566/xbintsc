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
  let logoPath: string;
  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-gui-"));
    // A 4x4 PNG: left half red, right half blue (base64-encoded inline).
    logoPath = join(workdir, "logo.png");
    writeFileSync(
      logoPath,
      Buffer.from(
        "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+AAAAFElEQVR42mP4z8DwH4Sh1H8G0gUALFAf4eNWqTEAAAAASUVORK5CYII=",
        "base64",
      ),
    );
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

  it("lays out block, inline and flex boxes", () => {
    const { stdout } = compileAndRun(
      "layout",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box { width: 200px; height: 100px; padding: 10px; border: 2px solid #000; margin: 5px; }
        #row { display: flex; width: 300px; gap: 10px; }
        #row .a { width: 50px; height: 20px; }
        #row .b { flex: 1; height: 20px; }
        #wrap { width: 60px; font-size: 10px; }
      </style></head><body>
        <div id="box">hello</div>
        <div id="row"><div class="a"></div><div class="b"></div></div>
        <div id="wrap">aaaa bbbb cccc dddd</div>
      </body></html>
      \`;

      const win = createWindow({ title: "layout", width: 800, height: 600 });
      win.on("ready", () => {
        const rect = (selector) => {
          const r = win.getBoundingClientRect(selector);
          return r.x + "," + r.y + "," + r.width + "," + r.height;
        };
        console.log("box=" + rect("#box"));
        console.log("a=" + rect("#row .a"));
        console.log("b=" + rect("#row .b"));
        console.log("wrap=" + rect("#wrap"));
        console.log("lh=" + win.fontMetrics(10).lineHeight);
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

    // Border box = content + padding (20) + border (4); margin offsets by 5.
    expect(value("box")).toBe("5,5,224,124");
    // Flex row: `.a` keeps 50px, `.b` grows into the remaining 240px after a 10px gap.
    expect(value("a")).toBe("0,134,50,20");
    expect(value("b")).toBe("60,134,240,20");
    // 60px-wide box wraps "aaaa bbbb" / "cccc dddd" onto two lines. The line
    // height comes from the real font metrics, so assert against them rather
    // than hard-coding a pixel value (fonts differ per platform).
    const wrap = value("wrap").split(",").map(Number);
    expect(wrap.slice(0, 3)).toEqual([0, 154, 60]);
    expect(wrap[3]).toBeCloseTo(2 * Number(value("lh")), 1);
  });

  it("sizes and paints images (intrinsic and explicit)", () => {
    const { stdout } = compileAndRun(
      "image",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #logo { width: 40px; height: 40px; }
      </style></head><body>
        <img id="logo" src="${logoPath}">
        <img id="natural" src="${logoPath}">
      </body></html>
      \`;

      const win = createWindow({ title: "image", width: 400, height: 300 });
      win.on("ready", () => {
        const logo = win.getBoundingClientRect("#logo");
        console.log("logo=" + logo.width + "x" + logo.height);
        const natural = win.getBoundingClientRect("#natural");
        console.log("natural=" + natural.width + "x" + natural.height);
        console.log(win.paintList().trim());
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    // Explicit CSS size wins; without one the intrinsic 4x4 size is used.
    expect(stdout).toContain("logo=40x40");
    expect(stdout).toContain("natural=4x4");
    expect(stdout).toContain(`image x=0.0 y=0.0 w=40.0 h=40.0 src=${logoPath}`);
    expect(stdout).toContain(`image x=0.0 y=40.0 w=4.0 h=4.0 src=${logoPath}`);
  });

  it("builds a display list of backgrounds and borders", () => {
    const { stdout } = compileAndRun(
      "paint",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; background-color: #123456; }
        #box { width: 100px; height: 40px; background: #ff0000; border: 4px solid #00ff00; border-radius: 8px; }
      </style></head><body>
        <div id="box">x</div>
      </body></html>
      \`;

      const win = createWindow({ title: "paint", width: 800, height: 600 });
      win.on("ready", () => {
        const r = win.getBoundingClientRect("#box");
        console.log("box-rect=" + r.x + "," + r.y + "," + r.width + "," + r.height);
        console.log("paint-count=" + win.paintCount());
        console.log("paint-list-start");
        console.log(win.paintList());
        console.log("paint-list-end");
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
    const list = stdout.slice(stdout.indexOf("paint-list-start"), stdout.indexOf("paint-list-end"));
    const occurrences = (needle: string): number => list.split(needle).length - 1;

    // Border box = 100x40 content + 4px border on every side.
    expect(value("box-rect")).toBe("0,0,108,48");
    // body background + #box background + 4 border edges.
    expect(value("paint-count")).toBe("6");
    expect(occurrences("#123456ff")).toBe(1); // body background
    expect(occurrences("#ff0000ff")).toBe(1); // #box background
    expect(occurrences("#00ff00ff")).toBe(4); // four border edges
    // The background rectangle carries the 8px corner radius.
    expect(list).toContain("r=8.0 color=#ff0000ff");
  });

  it("emits shaped text runs into the display list", () => {
    const { stdout } = compileAndRun(
      "text-runs",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #t { font-size: 20px; color: #0000ff; }
      </style></head><body>
        <div id="t">Hi</div>
      </body></html>
      \`;

      const win = createWindow({ title: "text-runs", width: 400, height: 300 });
      win.on("ready", () => {
        const r = win.getBoundingClientRect("#t");
        console.log("rect=" + r.x + "," + r.y + "," + r.width + "," + r.height);
        console.log("paint-list-start");
        console.log(win.paintList());
        console.log("paint-list-end");
      });
      win.loadHTML(HTML);
      run();
      `,
    );
    const list = stdout.slice(stdout.indexOf("paint-list-start"), stdout.indexOf("paint-list-end"));
    // The text fragment carries the resolved font size and colour.
    expect(list).toContain('text x=0.0');
    expect(list).toContain('size=20.0');
    expect(list).toContain('color=#0000ffff "Hi"');
  });

  it("hit-tests elements and delivers input events to handlers", () => {
    const { stdout } = compileAndRun(
      "input",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box { width: 100px; height: 50px; }
        #inner { width: 20px; height: 20px; }
      </style></head><body>
        <div id="box"><div id="inner">x</div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "input", width: 400, height: 300 });
      const log = [];
      win.on("mousemove", (e) => log.push("move:" + e.target));
      win.on("mousedown", (e) => log.push("down:" + e.button + ":" + e.target));
      win.on("click", (e) => log.push("click:" + e.x + "," + e.y + ":" + e.target));
      win.on("wheel", (e) => log.push("wheel:" + e.deltaX + "," + e.deltaY));
      win.on("keydown", (e) => log.push("key:" + e.key + ":" + e.shift));

      win.on("ready", () => {
        console.log("hit-inner=" + win.hitTest(5, 5));
        console.log("hit-box=" + win.hitTest(50, 40));
        console.log("hit-none=[" + win.hitTest(390, 290) + "]");
        win.sendEvent("mousemove", { x: 5, y: 5 });
        win.sendEvent("mousedown", { x: 5, y: 5, button: 0 });
        win.sendEvent("click", { x: 5, y: 5, button: 0 });
        win.sendEvent("wheel", { x: 5, y: 5, deltaX: 1, deltaY: 2 });
        win.sendEvent("keydown", { key: "a", code: "KeyA" });
        for (const line of log) console.log("event=" + line);
        win.close();
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

    // Hit testing returns the deepest element under the point.
    expect(value("hit-inner")).toBe("div#inner");
    expect(value("hit-box")).toBe("div#box");
    expect(value("hit-none")).toBe("[]");
    // Handlers receive the payload (coordinates, button, target, deltas, key).
    expect(stdout).toContain("event=move:div#inner");
    expect(stdout).toContain("event=down:0:div#inner");
    expect(stdout).toContain("event=click:5,5:div#inner");
    expect(stdout).toContain("event=wheel:1,2");
    expect(stdout).toContain("event=key:a:false");
  });

  it("applies :hover and :focus state to selector matching", () => {
    const { stdout } = compileAndRun(
      "state",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box { width: 100px; height: 50px; background: #000000; }
        #box:hover { background: #ff0000; }
        #box:focus { background: #00ff00; }
        #box:hover > #inner { color: #0000ff; }
        #inner { width: 20px; height: 20px; color: #888888; }
      </style></head><body>
        <div id="box"><div id="inner">x</div></div>
      </body></html>
      \`;

      const win = createWindow({ title: "state", width: 400, height: 300 });
      win.on("ready", () => {
        console.log("initial-bg=" + win.computedStyle("#box", "background-color"));
        console.log("initial-color=" + win.computedStyle("#inner", "color"));
        win.sendEvent("mousemove", { x: 50, y: 40 });
        console.log("hover-bg=" + win.computedStyle("#box", "background-color"));
        win.sendEvent("mousemove", { x: 5, y: 5 });
        console.log("inner-hover-color=" + win.computedStyle("#inner", "color"));
        win.sendEvent("mousedown", { x: 50, y: 40, button: 0 });
        console.log("focus-bg=" + win.computedStyle("#box", "background-color"));
        win.close();
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

    expect(value("initial-bg")).toBe("rgb(0, 0, 0)"); // no :hover rule match
    expect(value("initial-color")).toBe("rgb(136, 136, 136)");
    expect(value("hover-bg")).toBe("rgb(255, 0, 0)"); // hovering #box
    // Hover also applies to ancestors of the hovered element.
    expect(value("inner-hover-color")).toBe("rgb(0, 0, 255)");
    expect(value("focus-bg")).toBe("rgb(0, 255, 0)"); // :focus wins over :hover
  });

  it("shapes and measures text with the font stack", () => {
    const { stdout } = compileAndRun(
      "text",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "text", width: 400, height: 300 });
      win.on("ready", () => {
        const fm16 = win.fontMetrics(16);
        console.log("ready=" + fm16.ready);
        console.log("lh16=" + fm16.lineHeight);
        console.log("ascent16=" + fm16.ascent);
        console.log("m16=" + win.measureText("Hello", 16));
        console.log("m32=" + win.measureText("Hello", 32));
        console.log("empty=" + win.measureText("", 16));
        console.log("wide=" + (win.measureText("WWWW", 16) > win.measureText("iiii", 16)));
        win.close();
      });
      win.loadHTML("<html><body></body></html>");
      run();
      `,
    );
    const value = (label: string): string => {
      const line = stdout.split("\n").find((entry) => entry.startsWith(`${label}=`));
      expect(line, `missing ${label} in:\n${stdout}`).toBeDefined();
      return line!.slice(label.length + 1).trim();
    };

    // Metrics are positive and scale ~linearly with the font size (this holds
    // for both the real shaper and the no-font fallback).
    const m16 = Number(value("m16"));
    const m32 = Number(value("m32"));
    expect(m16).toBeGreaterThan(0);
    expect(m32).toBeGreaterThan(m16 * 1.8);
    expect(m32).toBeLessThan(m16 * 2.2);
    expect(Number(value("lh16"))).toBeGreaterThan(0);
    expect(Number(value("ascent16"))).toBeGreaterThan(0);
    expect(value("empty")).toBe("0");
    expect(value("wide")).toBe("true");
  });
});
