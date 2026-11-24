/**
 * End-to-end test for the `run()` frame pacing added in the performance work.
 *
 * A page that drives itself with `requestAnimationFrame` must get one frame per
 * display refresh, not one per idle loop iteration: the old loop drained the
 * rAF queue every few hundred microseconds, so a game ran its logic, WebGL
 * recording and DOM updates many times per presented frame. `XT_GUI_FRAME_MS`
 * pins the period on a machine-independent test (and `0` keeps the historical
 * uncapped behaviour for benchmarks).
 *
 * The program counts rAF callbacks for a fixed wall-clock window, so the
 * assertions are about the *period*, not about how fast the host is: a slow
 * software GPU only produces fewer frames, never more.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

/** One program shape: count frames for `WINDOW_MS`, then report and close. */
const COUNTING_PAGE = (windowMs: number): string => `
  import { createWindow, run } from "gui";

  const win = createWindow({ title: "pacing", width: 200, height: 150 });
  win.loadHTML("<html><body style='margin:0'></body></html>");

  let frames = 0;
  const tick = (): void => {
    frames = frames + 1;
    win.requestAnimationFrame(tick);
  };

  win.on("ready", () => {
    win.requestAnimationFrame(tick);
    setTimeout(() => {
      console.log("frames=" + frames);
      win.close();
    }, ${windowMs});
  });
  run();
`;

describe.skipIf(!guiAvailable)("gui extension — frame pacing", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("serves animation frames at the configured period", () => {
    const result = harness.compileAndRun("pacing-period", COUNTING_PAGE(800), {
      autocloseMs: 6000,
      console: true,
      env: { XT_GUI_FRAME_MS: "100" },
    });
    const frames = Number(result.value("frames"));
    /* 800 ms / 100 ms = 8 frames; the first one may start immediately and a
     * slow CI runner may miss a deadline, so accept a generous band. */
    expect(frames).toBeGreaterThanOrEqual(3);
    expect(frames).toBeLessThanOrEqual(14);
  }, 120000);

  it("does not pace when XT_GUI_FRAME_MS=0", () => {
    const result = harness.compileAndRun("pacing-uncapped", COUNTING_PAGE(800), {
      autocloseMs: 6000,
      console: true,
      env: { XT_GUI_FRAME_MS: "0" },
    });
    const frames = Number(result.value("frames"));
    /* The historical loop ran rAF on every iteration (SDL_Delay(1) when idle),
     * so an 800 ms window produces far more than the paced eight. The bound is
     * deliberately loose: the point is "not throttled to the period". */
    expect(frames).toBeGreaterThan(30);
  }, 120000);
});
