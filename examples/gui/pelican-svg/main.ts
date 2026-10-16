// 鹈鹕骑自行车 (SVG) — a pelican riding a bicycle, drawn as vector art and shown
// in a native window by the `gui` extension.
//
// The picture is `examples/resources/pelican-bike.svg`. It reaches the window
// the way a web page shows any picture — `<img id="art" src="...">` — and the
// engine decodes it with the **NanoSVG** parser/rasteriser vendored in
// `runtime/ext_gui/svg.cpp`, rasterising it at the drawn *device* size so the
// wheels and spokes stay crisp instead of being a magnified bitmap. (Inline
// `<svg>` trees are not supported by the engine; only SVG *files* are, which is
// exactly what this example uses.)
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

/** The window, a little larger than the 720x520 artwork. */
const WIDTH = 760;
const HEIGHT = 600;

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

/**
 * One report, printed from the first presented frame: what the window is (and
 * which GPU backend SDL picked), what the document laid out, and whether the
 * `<img>` really decoded. A missing or unreadable SVG would leave the same img
 * box (CSS sets width/height) but no image in the display list, so the paint
 * list is the evidence that the vector art rasterised.
 */
const report = (): void => {
  const art = win.getBoundingClientRect("#art");
  const painted = win.paintList().split("\n").filter((entry: string) => entry.indexOf("image ") === 0);

  line("window", WIDTH + "x" + HEIGHT + " (gui extension, SDL_GPU)");
  line("gpu", win.driver());
  line("art-box", "x=" + Math.round(art.x) + " y=" + Math.round(art.y) +
    " " + Math.round(art.width) + "x" + Math.round(art.height));
  line("paint-shapes", win.paintCount());
  line("painted-images", painted.length);
  if (painted.length > 0) line("painted-image", painted[0].trim());
  line("hit-test", win.hitTest(Math.round(art.x + art.width / 2), Math.round(art.y + art.height / 2)));
  reportArtwork();
  line("frames", frames);
};

let frames = 0;
let reported = false;

/**
 * Runs on the frame after the first one was presented, so the report reads a
 * document the engine has already laid out and painted once. The loop is not
 * re-queued: the artwork is static, and the window stays open until it is
 * closed (or `XT_GUI_AUTOCLOSE_MS` fires).
 */
const tick = (): void => {
  frames = frames + 1;
  if (!reported) {
    reported = true;
    report();
  }
};

win.on("ready", () => win.requestAnimationFrame(tick));
win.on("close", () => console.log("gui closed"));

win.loadHTML(documentForEngine());
run();
console.log("gui exited");
