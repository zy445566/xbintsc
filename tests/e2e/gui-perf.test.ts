/**
 * End-to-end test for the `gui` extension — rendering performance counters.
 *
 * A 200-row list (about 1,000 boxes plus text) is loaded, then one thing is
 * changed per frame: a text node, a class, an inline width, and a burst of
 * pointer moves. The engine reports what each frame actually did through
 * `win.stats()` (see `runtime/ext_gui/perf.h`), and the test pins the shape of
 * that work:
 *
 *   - a text/class/width change restyles once and repaints once;
 *   - a pointer move over a page without `:hover` rules does not restyle;
 *   - an idle frame lays out, paints, uploads and presents nothing (a present
 *     the window system asked for, or one a starved frame had to defer, is
 *     reported separately as `exposes`/`deferred` and is not counted as the
 *     page's work);
 *   - `@media` is evaluated once per rule, not once per rule and node;
 *   - unchanged text is shaped once and then served from the cache.
 *
 * Only counters are asserted (they are deterministic); the wall-clock numbers
 * are printed for a human and, on the machine that ran the suite, compared
 * against `XBINTSC_GUI_PERF_STRICT_MS` (unset in CI, where the GPU is software).
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

/** The row count of the generated page (three elements per row). */
const ROWS = 200;

type Fields = Record<string, string>;
type Phases = Record<string, Fields>;

/** Parse `perf.<phase> key=value ...` lines out of the program's stdout. */
function parsePhases(stdout: string): Phases {
  const phases: Phases = {};
  for (const line of stdout.split("\n")) {
    const match = /^(perf\.[a-z0-9]+)[ \t]+(.*)$/.exec(line);
    if (match === null) continue;
    const phase = match[1];
    const rest = match[2];
    if (phase === undefined || rest === undefined) continue;
    const fields: Fields = {};
    for (const token of rest.split(/\s+/)) {
      const equals = token.indexOf("=");
      if (equals > 0) fields[token.slice(0, equals)] = token.slice(equals + 1);
    }
    phases[phase] = fields;
  }
  return phases;
}

function count(phases: Phases, phase: string, key: string): number {
  const fields = phases[phase];
  if (fields === undefined) throw new Error(`missing ${phase} line in the program output`);
  const raw = fields[key];
  if (raw === undefined) throw new Error(`missing ${phase}.${key} in the program output`);
  const value = Number(raw);
  if (Number.isNaN(value)) throw new Error(`${phase}.${key} is not a number`);
  return value;
}

/**
 * Presents the page caused: every presented frame minus the ones the frame loop
 * had to attribute to something other than this frame's work.
 *
 * `exposes` is a redraw the *window system* asked for. A headless X server maps
 * the window whenever it gets around to it and then sends `Expose`, which the
 * engine must honour (the window has no contents) — that arrives at a time no
 * test can schedule, and only on a slow runner does it land inside the quiet
 * sample. `deferred` is a present a starved frame had to postpone: SDL's
 * non-blocking swapchain acquire skips a frame while the GPU still holds the
 * image, and the engine then presents the retained paint on the next frame (see
 * `runtime/ext_gui/perf.h`).
 */
function pagePresents(phases: Phases, phase: string): number {
  return (
    count(phases, phase, "presents") -
    count(phases, phase, "exposes") -
    count(phases, phase, "deferred")
  );
}

describe.skipIf(!guiAvailable)("gui extension — rendering performance", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("scales with the change instead of the whole document", () => {
    const result = harness.compileAndRun(
      "perf-stress",
      `
      import { createWindow, run } from "gui";

      const ROWS = ${ROWS};

      const CSS = \`
        * { margin: 0; padding: 0; }
        html, body { background-color: #05070a; color: #d0d8e0; font-size: 12px; }
        .row { display: flex; height: 22px; padding: 2px 6px; background-color: #101418; }
        .label { flex: 1; color: #d0d8e0; }
        .value { width: 80px; color: #6fd3a0; }
        .hot { background-color: #2a303a; }
        @media (max-width: 500px) { .row { height: 16px; } }
        @media (min-width: 500px) { .value { width: 90px; } }
      \`;

      let body = "";
      for (let i = 0; i < ROWS; i++) {
        body = body + '<div class="row"><span class="label">Item ' + i +
          '</span><span class="value">' + (1000 + i) + "</span></div>";
      }
      const HTML = "<html><head><style>" + CSS + "</style></head><body>" +
        "<div id='root'>" + body + "</div></body></html>";

      const win = createWindow({ title: "perf", width: 900, height: 700 });
      win.setBackground("#05070a");
      win.loadHTML(HTML);

      const doc = win.document;
      const labels = doc.querySelectorAll(".label");
      const values = doc.querySelectorAll(".value");
      const rows = doc.querySelectorAll(".row");

      const round = (value: number): number => Math.round(value * 100) / 100;

      /** Print the work the frame that just finished did. */
      const report = (label: string, before: any, after: any): void => {
        console.log(label +
          " restyles=" + (after.restyles - before.restyles) +
          " nodes=" + (after.restyleNodes - before.restyleNodes) +
          " rules=" + (after.rulesEvaluated - before.rulesEvaluated) +
          " media=" + (after.mediaEvaluated - before.mediaEvaluated) +
          " layouts=" + (after.layouts - before.layouts) +
          " boxes=" + (after.layoutBoxes - before.layoutBoxes) +
          " shapes=" + (after.textShapeCalls - before.textShapeCalls) +
          " hits=" + (after.textShapeHits - before.textShapeHits) +
          " paints=" + (after.paints - before.paints) +
          " ops=" + (after.paintOps - before.paintOps) +
          " runs=" + (after.drawRuns - before.drawRuns) +
          " draws=" + (after.drawCalls - before.drawCalls) +
          " uploads=" + (after.uploadSubmits - before.uploadSubmits) +
          " presents=" + (after.presents - before.presents) +
          " exposes=" + (after.exposes - before.exposes) +
          " deferred=" + (after.deferred - before.deferred) +
          " starved=" + (after.starvedFrames - before.starvedFrames) +
          " frames=" + (after.frames - before.frames) +
          " sync=" + (after.syncNodes - before.syncNodes) +
          " ms=" + round(after.frameMs) +
          " restyleMs=" + round(after.restyleMs) +
          " layoutMs=" + round(after.layoutMs) +
          " paintMs=" + round(after.paintMs) +
          " uploadMs=" + round(after.uploadMs) +
          " drawMs=" + round(after.drawMs));
      };

      const phases = ["perf.idle", "perf.text", "perf.class", "perf.width", "perf.hover"];
      let step = 0;
      let before: any = null;
      let tailBefore: any = null;
      let tailStarted = false;

      const apply = (index: number): void => {
        if (index === 1) labels[0].textContent = "Item 0 changed";
        else if (index === 2) rows[5].classList.add("hot");
        else if (index === 3) values[7].style.width = "120px";
        else if (index === 4) {
          for (let i = 0; i < 6; i++) win.sendEvent("mousemove", { x: 200, y: 40 + i * 22 });
        }
      };

      const finish = (): void => {
        const now: any = win.stats();
        report("perf.idle2", tailBefore, now);
        console.log("perf.page rows=" + ROWS + " labels=" + labels.length +
                    " treeNodes=" + now.treeNodes);
        win.close();
      };

      /* The quiet-period sample: the first timer snapshot still contains the
       * last animated frame, so take one more 60 ms apart and compare from
       * there. No animation frame is pending, so the engine must present
       * nothing while the counter still advances. */
      const tail = (): void => {
        if (tailStarted) return;
        tailStarted = true;
        tailBefore = win.stats();
        win.setTimeout(finish, 60);
      };

      const tick = (): void => {
        const now: any = win.stats();
        if (before !== null && step <= phases.length) report(phases[step - 1], before, now);
        if (step >= phases.length) {
          win.setTimeout(tail, 40);
          return;
        }
        before = now;
        apply(step);
        step = step + 1;
        win.requestAnimationFrame(tick);
      };

      win.on("ready", () => {
        console.log("perf.ready driver=" + win.driver());
        win.requestAnimationFrame(tick);
      });
      win.on("close", () => console.log("perf.closed"));
      run();
      console.log("perf.exit");
      `,
      { autocloseMs: 8000, console: true },
    );

    expect(result.stdout).toContain("perf.ready");
    expect(result.stdout).toContain("perf.closed");
    const phases = parsePhases(result.stdout);

    /* The generated page really is the large one. */
    expect(count(phases, "perf.page", "rows")).toBe(ROWS);
    expect(count(phases, "perf.page", "labels")).toBe(ROWS);
    expect(count(phases, "perf.page", "treeNodes")).toBeGreaterThan(ROWS * 4);

    /* First animated frame after the document settled: it presents, but the
     * document is not restyled, laid out or repainted again. The sample is a
     * single frame, so an expose the window system asked for in it coalesces
     * into the same present and does not change the count. If the GPU or the
     * compositor had no image for it, the frame is counted as `starved` instead
     * and its present happens on a later frame. */
    expect(count(phases, "perf.idle", "restyles")).toBe(0);
    expect(count(phases, "perf.idle", "layouts")).toBe(0);
    expect(count(phases, "perf.idle", "paints")).toBe(0);
    const idlePresented =
      count(phases, "perf.idle", "presents") + count(phases, "perf.idle", "starved");
    expect(idlePresented).toBe(1);
    /* Every presented frame issues one draw per run — the documented cost of
     * the current painter-order batching. */
    if (count(phases, "perf.idle", "presents") > 0) {
      expect(count(phases, "perf.idle", "draws")).toBeGreaterThanOrEqual(ROWS);
    }

    /* One text change: one restyle, one layout, one repaint. */
    expect(count(phases, "perf.text", "restyles")).toBe(1);
    expect(count(phases, "perf.text", "nodes")).toBeGreaterThan(ROWS * 2);
    expect(count(phases, "perf.text", "layouts")).toBe(1);
    expect(count(phases, "perf.text", "paints")).toBe(1);
    /* The repaint rebuilt the alternating runs, and the frame that drew them
     * issued one draw per run. A frame with no drawable target (no swapchain
     * image, a window with no drawable area) records no draws and is presented
     * again later instead — the engine keeps the paint pending, so the runs are
     * not lost; that is what `deferred` reports. */
    expect(count(phases, "perf.text", "runs")).toBeGreaterThanOrEqual(ROWS);
    const textDraws = count(phases, "perf.text", "draws");
    if (textDraws > 0) {
      expect(textDraws).toBe(count(phases, "perf.text", "runs"));
    }

    /* `@media` is evaluated once per rule, not once per rule and node. */
    expect(count(phases, "perf.text", "media")).toBeLessThanOrEqual(2);

    /* Text is shaped from the cache: only the changed words are new. */
    expect(count(phases, "perf.text", "hits")).toBeGreaterThan(ROWS);
    expect(count(phases, "perf.text", "shapes")).toBeLessThan(ROWS);

    /* A class change repaints; a width change reflows. */
    expect(count(phases, "perf.class", "paints")).toBe(1);
    expect(count(phases, "perf.width", "layouts")).toBe(1);

    /* Pointer moves over a document without `:hover` rules must not restyle. */
    expect(count(phases, "perf.hover", "restyles")).toBe(0);
    expect(count(phases, "perf.hover", "layouts")).toBe(0);

    /* Nothing changed across a quiet period: no restyle, no layout, no paint,
     * no upload, no present of the page's making and no draw, while frames keep
     * being served. A present the window system asked for (`exposes`) or a
     * starved frame deferred (`deferred`) is not the page's, but it does replay
     * the retained draws — so the draw assertion only applies to a quiet window
     * the compositor left alone. */
    expect(count(phases, "perf.idle2", "frames")).toBeGreaterThan(0);
    expect(count(phases, "perf.idle2", "restyles")).toBe(0);
    expect(count(phases, "perf.idle2", "layouts")).toBe(0);
    expect(count(phases, "perf.idle2", "paints")).toBe(0);
    expect(count(phases, "perf.idle2", "uploads")).toBe(0);
    expect(pagePresents(phases, "perf.idle2")).toBe(0);
    if (
      count(phases, "perf.idle2", "exposes") === 0 &&
      count(phases, "perf.idle2", "deferred") === 0
    ) {
      expect(count(phases, "perf.idle2", "draws")).toBe(0);
    }

    /* A human-readable wall-clock line, and an opt-in local budget. */
    console.log(
      "gui perf: " +
        `textFrame=${count(phases, "perf.text", "ms")}ms ` +
        `restyle=${count(phases, "perf.text", "restyleMs")}ms ` +
        `layout=${count(phases, "perf.text", "layoutMs")}ms ` +
        `paint=${count(phases, "perf.text", "paintMs")}ms ` +
        `upload=${count(phases, "perf.text", "uploadMs")}ms ` +
        `draw=${count(phases, "perf.text", "drawMs")}ms ` +
        `runs=${count(phases, "perf.text", "runs")}`,
    );
    const strictMs = Number(process.env.XBINTSC_GUI_PERF_STRICT_MS ?? "0");
    if (strictMs > 0) {
      expect(count(phases, "perf.text", "ms")).toBeLessThan(strictMs);
    }
  });
});
