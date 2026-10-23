// 鹈鹕骑自行车 (Canvas 2D) — the native host for the shared canvas demo.
//
// The picture, the simulation, the input handling and the frame loop live in
// `scene.js`. `index.html` loads that file with a plain `<script src>`, so the
// same document is a working demo in a browser: just open
// `examples/gui/pelican-canvas/index.html` — no build, server or bundler.
//
// xbintsc's gui `.html` loader reads the same `<script src>`, AOT-compiles the
// body and runs it after the native document has been parsed (there is no JS
// engine in the binary). This file therefore only opens the window, loads the
// document and prints a `label=value` report, including pixel probes read back
// from the canvas.
//
//   npx tsx src/cli/main.ts run examples/gui/pelican-canvas/main.ts
//   npx tsx src/cli/main.ts build examples/gui/pelican-canvas/main.ts
//
// Controls: Space/Up = jump, Right = sprint, Left = brake, R = reset,
// P = pause, Esc = quit (native window only). They work in a browser too,
// except that Esc cannot close an ordinary browser tab.
//
// On Windows the clang that ships with Visual Studio is new enough (19.x); the
// one on PATH may be 15 and rejects the emitted IR:
//
//   set xbintsc_CLANG=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang.exe
//
// Set XT_GUI_AUTOCLOSE_MS=<ms> to close the window after a delay (used for a
// headless smoke run).

import { createWindow, run } from "gui";
import page from "./index.html";

const WIDTH = 1000;
const HEIGHT = 640;

const win = createWindow({ title: "鹈鹕骑自行车 · Pelican Canvas", width: WIDTH, height: HEIGHT });
win.setBackground("#0d141f");

/** One `label=value` line, so a run (or a smoke test) can read the report. */
const line = (label: string, value: any): void => console.log(label + "=" + value);

let reported = false;

/** What the shared script drew, read back through its hooks. The pixel probes
 * prove that the gradients, paths and text reached the bitmap. */
const report = (game: any): void => {
  const info: any = game.info();
  const rect = win.getBoundingClientRect("#stage");
  line("window", WIDTH + "x" + HEIGHT + " (gui extension, SDL_GPU)");
  line("gpu", win.driver());
  line("canvas-box", "x=" + Math.round(rect.x) + " y=" + Math.round(rect.y) +
    " " + Math.round(rect.width) + "x" + Math.round(rect.height));
  line("canvas-size", info.canvas);
  line("frames", info.frames);
  line("state", "speed=" + Math.round(info.speed) + " distance=" + Math.floor(info.distance) +
    " airborne=" + info.airborne);
  line("paint-shapes", win.paintCount());
  line("pixel-sky", game.probe(500, 120));
  line("pixel-sun", game.probe(686, 118));
  line("pixel-road", game.probe(500, 620));
  line("pixel-pelican", game.probe(388, 286));
  line("pixel-wheel", game.probe(320, 516 - 70));
  line("pixel-title", game.probe(38, 56));
};

win.on("ready", () => {
  /* `loadHTML` has run the compiled `<script src>`, which published its
     hooks on the window. A browser has the same object on its `window`. */
  const game: any = (win as any).__pelicanCanvas;
  if (game === undefined || game === null) {
    line("script", "missing: index.html did not run scene.js");
    return;
  }
  /* Report on the first presented frame, after the script's own loop has
     drawn and advanced the simulation once. */
  const sample = (): void => {
    if (!reported) {
      reported = true;
      report(game);
    }
    win.requestAnimationFrame(sample);
  };
  win.requestAnimationFrame(sample);
});

win.on("close", () => console.log("gui closed"));

win.loadHTML(page);
run();
console.log("gui exited");
