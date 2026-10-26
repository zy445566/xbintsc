// WebGL 立方体 — the native host.
//
// The scene lives in `scene.js`, which `index.html` loads with a plain
// `<script src>`. A browser runs the same document unchanged; xbintsc
// AOT-compiles the script body and runs it after the native document is
// parsed. Nothing in the scene is rasterised on the CPU: the gui extension's
// WebGL context compiles the GLSL to SPIR-V, draws into an offscreen texture
// with SDL_GPU, and the page composites that texture directly.
//
//   npx tsx src/cli/main.ts run examples/gui/webgl-cube/main.ts
//   npx tsx src/cli/main.ts build examples/gui/webgl-cube/main.ts
//
// Set XT_GUI_AUTOCLOSE_MS=<ms> to close the window after a delay.

import { createWindow } from "gui";
import page from "./index.html";

const WIDTH = 800;
const HEIGHT = 640;

const win = createWindow({ title: "WebGL 立方体 · WebGL Cube", width: WIDTH, height: HEIGHT });
win.setBackground("#05070b");

const line = (label: string, value: any): void => console.log(label + "=" + value);

let reported = false;

win.on("ready", () => {
  const game: any = (win as any).__webglCube;
  if (game == null) {
    line("script", "missing: index.html did not run scene.js");
    return;
  }
  const sample = (): void => {
    if (!reported && game.frames > 2) {
      reported = true;
      line("window", WIDTH + "x" + HEIGHT + " (gui extension, SDL_GPU)");
      line("gpu", win.driver());
      line("webgl", game.ok);
      line("canvas", game.size);
      line("frames", game.frames);
      line("error", game.error);
      line("pixel-center", game.probe(400, 320));
      line("pixel-max", game.probeMax(200, 160, 400, 320));
    }
    win.requestAnimationFrame(sample);
  };
  win.requestAnimationFrame(sample);
});

win.loadHTML(page);
