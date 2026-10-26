// WebGL 2 立方体阵列 — the native host.
//
// `scene.js` draws 64 lit, textured cubes with one instanced draw call from a
// GLSL ES 3.00 vertex shader (`#version 300 es`), one VAO and a per-instance
// attribute divisor. A browser runs the same document unchanged; xbintsc
// AOT-compiles the script body and composites the WebGL colour texture with no
// CPU readback.
//
//   npx tsx src/cli/main.ts run examples/gui/webgl2-cubes/main.ts
//   npx tsx src/cli/main.ts build examples/gui/webgl2-cubes/main.ts
//
// Set XT_GUI_AUTOCLOSE_MS=<ms> to close the window after a delay.

import { createWindow } from "gui";
import page from "./index.html";

const WIDTH = 800;
const HEIGHT = 640;

const win = createWindow({ title: "WebGL 2 · 64 cubes", width: WIDTH, height: HEIGHT });
win.setBackground("#05070b");

const line = (label: string, value: any): void => console.log(label + "=" + value);

let reported = false;

win.on("ready", () => {
  const game: any = (win as any).__webgl2Cubes;
  if (game == null) {
    line("script", "missing: index.html did not run scene.js");
    return;
  }
  const sample = (): void => {
    if (!reported && game.frames > 2) {
      reported = true;
      line("window", WIDTH + "x" + HEIGHT + " (gui extension, SDL_GPU)");
      line("gpu", win.driver());
      line("webgl2", game.ok);
      line("canvas", game.size);
      line("instances", game.instances);
      line("frames", game.frames);
      line("error", game.error);
      line("pixel-center", game.probe(400, 320));
      line("pixel-max", game.probeMax(150, 120, 500, 400));
    }
    win.requestAnimationFrame(sample);
  };
  win.requestAnimationFrame(sample);
});

win.loadHTML(page);
