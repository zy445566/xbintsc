// Pelican on a bike — a small 2D game written as an HTML/CSS document whose
// game loop is AOT-compiled TypeScript.
//
// Everything the game needs lives in `index.html`: the markup, the stylesheet
// and an inline `<script lang="ts">`. The gui `.html` asset loader compiles that
// script body to native code (there is no JavaScript engine) and `loadHTML`
// runs it once the document is parsed; the script drives the game from
// `window.requestAnimationFrame` and the window input events.
//
// This file only opens the window, loads the document, starts the loop and
// prints a short geometry report so a run proves the scene laid out. The report
// uses the `label=value` shape so the same output can be asserted by tests
// (see `tests/e2e/gui-example.test.ts`).
//
//   xbintsc run examples/gui/pelican-bike/main.ts
//   xbintsc build examples/gui/pelican-bike/main.ts --out build
//
// Controls: Space/Up = jump, Right = pedal harder, Left = brake, P = autoplay,
// R = restart, Esc = quit.
//
// From the repository, inside an x64 Native Tools Command Prompt for VS (clang
// needs the MSVC/SDK environment on Windows):
//
//   set xbintsc_CLANG=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang.exe
//   npx tsx src/cli/main.ts run examples/gui/pelican-bike/main.ts
//
// Set XT_GUI_AUTOCLOSE_MS=<ms> to make the engine close the window after a
// delay; the geometry report below runs once per window either way.

import { createWindow, run } from "gui";
import page from "./index.html";

const WIDTH = 1000;
const HEIGHT = 660;

/** Frames between two progress lines, so a long run stays observable. */
const REPORT_EVERY_FRAMES = 300;
/** The first report comes much earlier: a slow host (a loaded machine, a
 * software GPU) may only present a few dozen frames before the auto-close
 * guard ends the run, and the smoke test needs the report from it. */
const FIRST_REPORT_FRAME = 60;

const win = createWindow({ title: "鹈鹕骑自行车 · Pelican Bike", width: WIDTH, height: HEIGHT });
win.setBackground("#0d141f");

/** One `label=value` line, so a run (or a test) can read the report back. */
const line = (label: string, value: any): void => console.log(label + "=" + value);

/** The box of a scene element, in viewport coordinates. */
const box = (id: string): string => {
  const rect = win.document.getElementById(id).getBoundingClientRect();
  return (
    "x=" + Math.round(rect.x) + " y=" + Math.round(rect.y) +
    " " + Math.round(rect.width) + "x" + Math.round(rect.height)
  );
};

let frames = 0;

/**
 * The geometry report: what a smoke test reads back, and a progress line for a
 * long interactive run. `win.close()` is only ever called by the harness'
 * `XT_GUI_AUTOCLOSE_MS` guard or by the player, so the window is never taken
 * away mid-game.
 */
const report = (): void => {
  const game: any = (win as any).__pelican;
  const info: any = game === undefined || game === null ? {} : game.info();
  line("window", WIDTH + "x" + HEIGHT + " (gui extension, SDL_GPU)");
  line("gpu", win.driver());
  line("frames", frames);
  line("title", win.document.querySelector(".title").textContent);
  line("scene", box("scene"));
  line("sky", box("sky"));
  line("road", box("road"));
  line("pelican", box("pogo"));
  line("rider", box("rider"));
  line("shadow", box("shadow"));
  line("hud", info.hud);
  line("ground", info.ground);
  line("player-box", info.playerBox);
  line("last-hit", info.hit);
  line("handle-identity", info.identity);
  line("paint-shapes", win.paintCount());
  line("hit-test", win.hitTest(500, 560));
};

win.on("ready", () => {
  line("script", "inline AOT <script> ran");

  /* The game owns its own frame loop (started by the document's script); this
     second callback only counts frames and reports periodically. */
  const sample = () => {
    frames = frames + 1;
    if (frames === FIRST_REPORT_FRAME || frames % REPORT_EVERY_FRAMES === 0) report();
    win.requestAnimationFrame(sample);
  };
  win.requestAnimationFrame(sample);
});

win.on("close", () => console.log("gui closed"));
win.loadHTML(page);
run();
console.log("gui exited");
