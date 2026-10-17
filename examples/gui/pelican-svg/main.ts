// 鹈鹕骑自行车 (SVG) — a pelican riding a bicycle, drawn as vector art, shown in
// a native window by the `gui` extension and **animated** with CSS transforms.
//
// The picture is `examples/resources/pelican-bike.svg`. It reaches the window
// the way a web page shows any picture — `<img id="art" src="...">` — and the
// engine decodes it with the **NanoSVG** parser/rasteriser vendored in
// `runtime/ext_gui/svg.cpp`, rasterising it at the drawn *device* size so the
// wheels and spokes stay crisp instead of being a magnified bitmap. (Inline
// `<svg>` trees are not supported by the engine; only SVG *files* are, which is
// exactly what this example uses.)
//
// The motion comes from CSS `transform`, a paint-time effect that never reflows
// the page. Two mechanisms drive it:
//
//   * `requestAnimationFrame` rewrites `#art.style.transform` every frame — a
//     呼吸/摇摆 pose (`translateY(...) rotate(...) scale(...)`, pivoted at
//     `transform-origin: 50% 92%`, i.e. where the wheels touch the road).
//   * `:hover` on the caption is animated by a CSS **transition** on `transform`
//     (it hops up and fades in), and the card's border colour transitions too.
//
// Clicking the artwork pauses/resumes the per-frame pose (a `class` toggle plus
// a transitioned caption colour), which is the smallest possible demonstration
// of "input → DOM mutation → restyle → repaint".
//
// So this program is small on purpose: it opens the window, loads the document,
// cross-checks the artwork against what the engine actually laid out, and prints
// a `label=value` report. The whole document is compiled into the binary as a
// string (`<script>` bodies included); the SVG file is the only thing read from
// disk at runtime.
//
//   npx tsx src/cli/main.ts run examples/gui/pelican-svg/main.ts
//   npx tsx src/cli/main.ts build examples/gui/pelican-svg/main.ts -o build/pelican-svg/pelican-svg.exe
//
// `index.html` points at `pelican-bike.svg` *next to itself*, which is what a
// browser and a static host need — so the build copies the artwork out of
// `examples/resources/` beside the executable (see README). The engine cannot
// use that spelling: it resolves `src` against the process working directory and
// knows nothing about the document's location (`import.meta` has no value here,
// and neither `process.execPath` nor `chdir` exists), so this program finds the
// real file and rewrites the attribute to an absolute path before `loadHTML`.
// It looks, in order, beside the working directory (double-clicking the
// executable, or building into the example folder), then beside the source
// checkout when run from the repository root.
//
// On Windows, clang needs the MSVC/SDK environment, so start in an x64 Native
// Tools Command Prompt and point xbintsc at a clang >= 16 — the `clang` on PATH
// here is 15 and rejects the emitted IR:
//
//   set xbintsc_CLANG=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang.exe
//
// Set XT_GUI_AUTOCLOSE_MS=<ms> to close the window after a delay; that is how a
// headless smoke run exercises this example (the report is printed either way).

import { existsSync, readFileSync } from "node:fs";
import { resolve } from "node:path";
import { createWindow, run } from "gui";
import page from "./index.html";

/** The window. The card around the artwork is 742x542 (720x520 + padding and
 * border), and `body` pads 24px top/bottom and 29px left/right, so 800x640
 * leaves the card centred with room for the caption. */
const WIDTH = 800;
const HEIGHT = 640;

/** The file name `index.html` uses, and the name of the copy that ships beside
 * the executable (see README). */
const ART_NAME = "pelican-bike.svg";
/** The canonical artwork in the source tree. */
const ART_SOURCE = "examples/resources/pelican-bike.svg";

/**
 * The engine has no document base URL, no `import.meta`, no `process.execPath`
 * and no `chdir`, so it cannot know where `index.html` lives: `xt_image_load`
 * opens `src` relative to the process working directory alone. This program
 * therefore locates the artwork itself and hands the engine an absolute path.
 *
 * The working-directory spellings are tried first, because that is what a
 * double-clicked executable and a build inside the example folder both give;
 * `ART_SOURCE` covers running from the repository root, where the canonical file
 * still lives. `undefined` means none of them exists, and the report says so
 * rather than claiming the artwork rendered.
 */
const resolveArt = (): string | undefined => {
  const candidates = [
    resolve(process.cwd(), ART_NAME),
    resolve(ART_NAME),
    resolve(process.cwd(), "..", "..", "..", ART_SOURCE),
    resolve(ART_SOURCE),
  ];
  for (const candidate of candidates) {
    if (existsSync(candidate)) return candidate;
  }
  return undefined;
};

const artPath = resolveArt();

/**
 * `index.html`'s relative `src` -> the absolute path the engine can open.
 *
 * A native path, not a `file://` URL: on Windows the engine's own `file://`
 * handling treats the third slash of `file:///D:/x` as an authority separator
 * and hands the OS `/D:/x`, which fails to open — while the path in this
 * attribute is the same one `readFileSync` below already proved readable.
 */
const documentForEngine = (): string => {
  if (artPath === undefined) return page;
  return page.split(ART_NAME).join(artPath.split("\\").join("/"));
};

const win = createWindow({ title: "鹈鹕骑自行车 · Pelican Bike (SVG)", width: WIDTH, height: HEIGHT });
win.setBackground("#101a26");

/** `label=value`, so a run (or a smoke test) can read the report back. */
const line = (label: string, value: string | number | boolean): void => console.log(label + "=" + value);

/* -- the animation -------------------------------------------------------- */

/** Amplitudes of the per-frame pose, in px / degrees / ratio. */
const BOB_PX = 5;
const SWAY_DEG = 1.1;
const BREATH = 0.014;

/** One pose, as a CSS `transform` list. `scale`/`rotate`/`translate` compose in
 * order — the engine parses the list into one matrix per frame. */
const pose = (time: number): string => {
  const seconds = time / 1000;
  const bob = Math.sin(seconds * 2.4) * BOB_PX;
  const sway = Math.sin(seconds * 1.15) * SWAY_DEG;
  const breath = 1 + Math.sin(seconds * 2.4 + 1.1) * BREATH;
  return "translateY(" + bob.toFixed(2) + "px) rotate(" + sway.toFixed(2) +
    "deg) scale(" + breath.toFixed(3) + ")";
};

/** `width="720"` / `height="520"` out of the SVG's root element. */
const reportArtwork = (): void => {
  if (artPath === undefined) {
    line("svg-file", "not found: no " + ART_NAME + " beside the working directory, no " + ART_SOURCE);
    return;
  }
  const source = readFileSync(artPath, "utf8");
  const w = /width="(\d+(?:\.\d+)?)"/.exec(source);
  const h = /height="(\d+(?:\.\d+)?)"/.exec(source);
  line("svg-file", artPath);
  line("svg-declares", (w === null ? "?" : w[1]) + "x" + (h === null ? "?" : h[1]));
  line("svg-bytes", source.length);
  line("svg-has-viewbox", /viewBox="/.test(source));
  line("svg-paths", source.split("<path").length - 1);
  line("svg-stops", source.split("<stop").length - 1);
};

let frames = 0;
let reported = false;

/**
 * One report, printed from the first presented frame, *before* the animation
 * touches the document: what the window is (and which GPU backend SDL picked),
 * what the document laid out, and whether the `<img>` really decoded. A missing
 * or unreadable SVG would leave the same img box (CSS sets width/height) but no
 * image in the display list, so the paint list is the evidence that the vector
 * art rasterised.
 *
 * `art-box` is therefore the *untransformed* layout box (40,35 720x520); the
 * `art-pose@…` lines that follow report the animated, transformed geometry.
 */
const report = (): void => {
  const artRect = win.getBoundingClientRect("#art");
  const painted = win.paintList().split("\n").filter((entry: string) => entry.indexOf("image ") === 0);

  line("window", WIDTH + "x" + HEIGHT + " (gui extension, SDL_GPU)");
  line("gpu", win.driver());
  line("art-box", "x=" + Math.round(artRect.x) + " y=" + Math.round(artRect.y) +
    " " + Math.round(artRect.width) + "x" + Math.round(artRect.height));
  line("paint-shapes", win.paintCount());
  line("painted-images", painted.length);
  if (painted.length > 0) line("painted-image", painted[0].trim());
  line("hit-test", win.hitTest(Math.round(artRect.x + artRect.width / 2), Math.round(artRect.y + artRect.height / 2)));
  line("art-origin", win.computedStyle("#art", "transform-origin"));
  reportArtwork();
};

/**
 * Called once the first frame is on screen, so the document is parsed and laid
 * out and the element handles exist. It owns the animation:
 *
 *   - `tick` rewrites `#art`'s `transform` every frame from the frame clock;
 *   - `:hover` on the caption and on the card's border is animated by the CSS
 *     transitions in `index.html`;
 *   - clicking the artwork toggles `body.paused`, which stops the per-frame pose
 *     and (through another transition) greys the caption.
 *
 * The report runs on the first frame; the first two frames also print the pose
 * they applied, plus the geometry and the matrix it produced, which is the
 * evidence that the artwork really moves and that the transform reached the
 * paint list.
 */
const start = (): void => {
  const art = win.document.getElementById("art");
  const body = win.document.querySelector("body");
  const caption = win.document.getElementById("caption");

  let paused = false;
  const togglePause = (): void => {
    paused = !paused;
    body.classList.toggle("paused");
    caption.textContent = "鹈鹕骑自行车 · Pelican on a Bicycle — SVG" + (paused ? "（暂停）" : "");
  };
  art.addEventListener("click", togglePause);

  const tick = (time: number): void => {
    frames = frames + 1;
    if (!reported) {
      reported = true;
      report();
    }
    if (!paused) art.style.setProperty("transform", pose(time));
    if (frames <= 2) {
      line("art-pose@" + frames, win.computedStyle("#art", "transform"));
      if (frames === 1) {
        /* The transformed geometry and the matrix the renderer will use: the
           pose is a paint-time effect, so the *layout* box never changed. */
        const moved = win.getBoundingClientRect("#art");
        line("art-moved-box", "x=" + Math.round(moved.x) + " y=" + Math.round(moved.y) +
          " " + Math.round(moved.width) + "x" + Math.round(moved.height));
        const painted = win.paintList().split("\n").filter(
          (entry: string) => entry.indexOf("image ") === 0);
        const suffix = painted.length > 0 ? painted[0].split(" transform=") : [];
        line("art-matrix", suffix.length > 1 ? suffix[1] : "none");
      }
    }
    win.requestAnimationFrame(tick);
  };
  win.requestAnimationFrame(tick);
};

win.on("ready", start);
win.on("close", () => console.log("gui closed"));

win.loadHTML(documentForEngine());
run();
console.log("gui exited");
