// Pelican Ride 3D — the native host.
//
// The scene, the animation, the WebGL 2 renderer and the synthesized sound all
// live in the ES modules under `js/`; `index.html` loads `js/main.js` as a
// module script, exactly like a browser. This file only opens the window, loads
// the page and prints a small `label=value` report so a run is self-verifying.
//
//   set xbintsc_CLANG=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang.exe
//   npx tsx src/cli/main.ts run examples/gui/pelican-3d/main.ts
//   npx tsx src/cli/main.ts build examples/gui/pelican-3d/main.ts
//
// Set XT_GUI_AUTOCLOSE_MS=<ms> to close the window after a delay.

import { createWindow, run } from "gui";
import page from "./index.html";

const WIDTH = 1100;
const HEIGHT = 700;

const win = createWindow({ title: "鹈鹕骑单车 3D · Pelican Ride", width: WIDTH, height: HEIGHT });
win.setBackground("#0d141f");

const line = (label: string, value: any): void => console.log(label + "=" + value);

let reported = false;

win.on("ready", () => {
  const game: any = (win as any).__pelicanRide3d;
  if (game === undefined || game === null) {
    line("script", "missing: index.html did not run js/main.js");
    return;
  }

  const sample = (): void => {
    if (!reported && game.frames >= 12) {
      reported = true;
      const info: any = game.info();
      line("window", WIDTH + "x" + HEIGHT + " (gui extension, SDL_GPU)");
      line("gpu", win.driver());
      line("webgl2", info.ok === true);
      line("gl-version", info.glVersion);
      line("glsl-version", info.glslVersion);
      line("canvas", info.size);
      line("modules", 9);
      line("frames", info.frames);
      line("fps", Math.round(info.fps));
      line("draws", info.draws);
      line("triangles", info.triangles);
      line("speed", Math.round(info.speed * 10) / 10);
      line("distance", Math.floor(info.distance));
      line("mode", info.mode);
      line("sound", info.sound);
      line("audio-state", info.audioState);
      line("error", info.ok === true ? 0 : (game.error ?? "unknown"));
      line("pixel-sky", game.probe(550, 650));
      line("pixel-road", game.probe(550, 90));
      line("pixel-center", game.probe(550, 350));
      line("pixel-max", game.probeMax(280, 220, 520, 330));
    }
    win.requestAnimationFrame(sample);
  };
  win.requestAnimationFrame(sample);
});

win.on("close", () => console.log("gui closed"));
win.loadHTML(page);
run();
console.log("gui exited");
