# xbintsc GUI extension (self-hosted HTML/CSS renderer)

Status: **M10** — features (M1–M10) are complete: HTML parsing, CSS selector
matching, the cascade, computed styles and layout (block, inline and Flexbox) are
in place, and the engine *paints*: it builds a display list of rectangles, images
and shaped text runs and renders them through SDL_GPU. Input events are hit
tested and delivered to native TS handlers, `:hover`/`:focus` are matched
dynamically, `<img>` is sized from its intrinsic dimensions and drawn from a
texture, and CSS transitions animate paint properties. **M8** adds an interactive
DOM: element handles with stable identity, mutation (`appendChild`, `textContent`,
`classList`, `style`, …) and element-level events with capture/bubbling.
**M9** compiles `<script>` bodies ahead of time (inline and `<script src>`) — see
`doc/gui-scripts.md`. **M10** adds `requestAnimationFrame` plus a few DOM helpers.
This document records the locked decisions, the architecture, the milestone plan
and the current progress of a cross-platform GUI extension that renders an
HTML/CSS UI with its own GPU-accelerated engine.

## Goals

1. **Cross-platform GUI** from TypeScript compiled by xbintsc to a native
   binary: macOS, Linux, Windows.
2. **HTML5/CSS rendering** with a **self-written** engine (parser, cascade,
   layout, paint), not a system WebView or an embedded browser.
3. **GPU accelerated** — hardware rasterization/compositing is a hard
   requirement, not an optimization.
4. **Multiple windows.**
5. The engine must not compromise xbintsc's nature as a binary compiler: the
   core compiler, lexer, parser, binder and code generator must stay
   platform-agnostic and never grow GUI branches.

## Non-goals (for now)

- Executing **runtime** page `<script>` (scripts fetched over the network or
  created dynamically). **Compile-time** scripts are AOT-compiled by xbintsc and
  do run — see `doc/gui-scripts.md`. Logic may also live in native TS called
  back through `xt_call_with_this`.
- A JS engine (QuickJS/V8/...). Explicitly out of scope; there is no runtime
  interpreter/JIT, so `<script>` bodies are compiled ahead of time.
- Full web compatibility / a browser. We implement a practical HTML/CSS subset.

## Locked decisions

| # | Decision |
| --- | --- |
| 1 | **No page JS engine.** Behaviour is native TS, called back through `xt_call_with_this`. `<script>` bodies are **AOT-compiled** by xbintsc itself (no interpreter) — see `doc/gui-scripts.md`. |
| 2 | Third-party **low-level** libraries are allowed (GPU backend, text shaping, image decode). HTML/CSS parsing + layout + paint scheduling are self-written. |
| 3 | **GPU acceleration is mandatory.** |
| 4 | The engine is **not self-hosted** (it is C/C++, not TS), but must not affect the compiler's platform-agnostic design. Delivered as a per-platform prebuilt archive linked via `nativeObjects`. |
| 5 | The **event loop is generic**: the runtime exposes a generic main-loop hook and a poll primitive; nothing GUI-specific enters `runtime/`. |
| 6 | **Multiple windows** are supported by the core object model. |
| 7 | Third-party low-level libs are **statically linked into `gui.a`** so releases stay self-contained; only OS frameworks are added at link time. |

## Architecture

```
              TypeScript (compiled by xbintsc to native code)
                │  import { createWindow, run } from "gui"
                ▼
        gui extension bindings  (src/extensions/gui)
                │  xt_gui_* symbols  (uniform (argc, argv) ABI)
                ▼
             gui.a                  ── the self-hosted engine (C/C++)
   ┌───────────────┬──────────────────┬───────────────────┐
   ▼               ▼                  ▼                   ▼
 HTML parser   CSS cascade +      Layout             GPU compositor
 (subset)      selector match  (block/inline/flex)  (SDL_GPU: Metal/
                                                      Vulkan/D3D12)
                │
                ▼
        windowing + input (SDL3) ── multiple windows
        text (HarfBuzz + FreeType) ── glyph atlas / SDF
        images (stb_image)
```

The engine never talks to the compiler. The compiler only sees an `Extension`
with `nativeObjects()`, `linkerFlags()` and `modules()`.

## Stack (confirmed)

| Concern | Choice | Why |
| --- | --- | --- |
| Window + input + multi-window | **SDL3** | cross-platform windows, HiDPI, IME, clipboard, DnD, close/hide events |
| GPU | **SDL_GPU** (SDL3) | one render path over Metal / Vulkan / D3D12; avoids three backends |
| Text shaping | **HarfBuzz** | correct complex-script shaping |
| Glyph raster | **FreeType** | glyph outlines → GPU atlas / SDF |
| Images | **stb_image** (vendored header) | single header to start |

Pinned versions: SDL3 `release-3.2.10`, FreeType `2.13.3`, HarfBuzz `10.1.0`
(overridable with `SDL3_TAG` / `FREETYPE_VERSION` / `HARFBUZZ_VERSION`).

Alternative if SDL3 is rejected: **GLFW + OpenGL 3.3** (simpler, but OpenGL is
deprecated on macOS and gives no modern GPU abstraction). **wgpu-native** if the
engine were Rust.

> These vendored libraries are linked **statically into `gui.a`** so a released
> xbintsc stays "download and run"; only OS frameworks are added by
> `linkerFlags()`.

## TS-facing API

```ts
import { createWindow, run, quit } from "gui";

const win = createWindow({ title: "Demo", width: 900, height: 600 });
win.setBackground("#14161c");
win.loadHTML(INDEX_HTML);              // parses HTML/CSS and computes styles
win.on("ready", () => console.log("first frame presented"));
win.on("close", () => console.log("window closed"));

run();                                 // drives the main loop until all windows close
```

Methods implemented on a window handle: `setTitle` / `setSize` / `loadHTML` /
`getHTML` / `setBackground` / `close` / `isOpen` / `on` / `off`. Events emitted:
`ready` (after the first presented frame), `load`, `close`, and the input events
`mousemove`, `mousedown`, `mouseup`, `click`, `wheel`, `keydown`, `keyup`.

Input handlers receive a single payload object (lifecycle handlers receive none):

```ts
win.on("click", (e) => console.log(e.x, e.y, e.button, e.target));
win.on("wheel", (e) => console.log(e.deltaX, e.deltaY));
win.on("keydown", (e) => console.log(e.key, e.code, e.ctrl, e.shift, e.alt, e.meta));
```

| Field | Events | Meaning |
| --- | --- | --- |
| `x`, `y` / `clientX`, `clientY` | pointer, wheel | viewport-relative logical pixels |
| `button` | pointer | `0` left, `1` middle, `2` right (`-1` for move) |
| `clicks` | pointer | click count reported by the OS |
| `deltaX`, `deltaY` | wheel | scroll amount (`deltaY` positive = down) |
| `key`, `code` | keyboard | key name and physical scancode name |
| `repeat`, `ctrl`, `shift`, `alt`, `meta` | keyboard | modifiers |
| `target` | pointer, wheel | deepest element under the point as a CSS-like descriptor (`div#main.card`), or absent |

### Implemented HTML/CSS subset (M3a)

**HTML parser** (`runtime/ext_gui/dom.{h,cpp}`): tags/attributes/text,
comments and doctype skipped, entity decoding (named + numeric/hex), void
elements, raw-text elements (`<style>`/`<script>`), and the common implicit
close rules (`li`, `dt`/`dd`, `option`, `p`, headings, table cells/rows).

**CSS parser** (`runtime/ext_gui/css.{h,cpp}`): comments and at-rules skipped
(for now), rules with multiple selectors, and declarations. Selectors: type,
`.class`, `#id`, attribute (`=`, `~=`, `|=`, `^=`, `$=`, `*=`), the four
combinators (descendant, child, adjacent and general sibling) and the
pseudo-classes `:first-child`, `:last-child`, `:only-child`, `:empty`,
`:root`, `:not(...)`, `:nth-child(an+b)`, `:disabled`, `:checked`, plus the
stateful `:hover` and `:focus` (see *Implemented input*). Values:
lengths (`px`, `%`, `em`, `rem`, `vw`, `vh`, `pt`, `pc`, `in`, `cm`, `mm`, `q`),
colors (hex, `rgb()`/`rgba()`, a named subset), numbers, keywords and shorthands
(`margin`/`padding`/`border`/`flex`).

**Cascade & computed style** (`runtime/ext_gui/style.{h,cpp}`): a small built-in
UA stylesheet, author rules sorted by `(!important, specificity, source order)`,
then inline `style=""` (highest specificity, but non-`!important` inline loses to
`!important`), plus CSS inheritance of the text properties. Relative lengths are
kept unresolved until layout, except `font-size` (resolved against the *parent*
font size) and `line-height`.

### Diagnostics

So the HTML/CSS/paint pipeline is testable without a GPU, a window handle
exposes read-only hooks:

```ts
win.computedStyle(selector, property)  // e.g. ("#main", "width") -> "60%"
win.queryCount(selector)               // number of matching elements
win.getBoundingClientRect(selector)    // { x, y, width, height } (border box)
win.documentTree()                     // serialized DOM (debugging)
win.layoutTree()                       // serialized layout boxes (debugging)
win.paintCount()                       // number of shapes in the display list
win.paintList()                        // serialized display list (debugging)
win.measureText(text, fontSize?, family?)  // shaped advance width in pixels
win.fontMetrics(fontSize?, family?)    // { ascent, descent, lineHeight, ready }
win.hitTest(x, y)                      // deepest element descriptor, or ""
win.sendEvent(type, options?)          // synthesise input (testing)
win.advance(ms)                        // step the CSS transition clock (testing)
```

They are used by `tests/e2e/gui-*.test.ts` to assert parsing, selector matching,
specificity, inheritance, `!important` and layout geometry. They will stay useful
afterwards for debugging.

### Interactive DOM (M8)

`win.document` returns the document handle; element handles have stable identity
and read/write properties, attributes, traversal and geometry:

```ts
const doc = win.document;
const box = doc.querySelector("#box");
const inner = doc.querySelector("#inner");
inner.textContent = "hi";              // write
inner.classList.add("hot");
inner.style.setProperty("color", "#0f0");
inner.setAttribute("data-role", "lead");
console.log(inner.id, inner.tagName, doc.querySelector("#box") === box);

const created = doc.createElement("div");
created.textContent = "added";
box.appendChild(created);
box.removeChild(created);

inner.addEventListener("click", (e) => console.log(e.target.id, e.currentTarget.id));
inner.click();                          // synthesise a click at the element
console.log(box.offsetWidth, box.offsetHeight);   // rounded border box
console.log(box.contains(inner));                 // descendant test
```

Mutations mark the document dirty; the engine restyles + relayouts lazily (before
the next read or frame). `inner = …`/`innerHTML = …` do **not** run scripts.
Element events support capture and bubble phases, `stopPropagation`, `once`, and
bubble up to `document`/`window`. The legacy `win.on(type, fn)` payload keeps its
**string** `e.target` (`div#id.class`); the element `Event.target` is a handle
whose descriptor matches the same string.

### AOT scripts (M9)

Import an `.html` file that contains inline `<script lang="ts">` bodies; the
loader compiles each body into a native function and `win.loadHTML(page)` runs
them once the document is parsed (no JavaScript engine, no runtime `eval`):

```ts
import { createWindow, run } from "gui";
import page from "./page.html";

const win = createWindow({ title: "counter", width: 320, height: 240 });
win.loadHTML(page);
run();
```

```html
<button id="b">0</button>
<script lang="ts">
  const b = document.getElementById("b");
  let n = 0;
  b.addEventListener("click", () => { b.textContent = String(++n); });
</script>
```

The two parameters (`window`, `document`) are ordinary function arguments, so
script locals need no global-object machinery. External scripts work too:
`<script src="./counter.ts">` is read at compile time, its imports are hoisted
(rewritten to resolve from the HTML file) and its body is wrapped the same way —
so a script module can `import` helpers and still see `document`. Scripts are
**compile-time assets**: HTML created at runtime (`innerHTML`, fetched over the
network) never executes, and `src` URLs (`https://…`, `data:…`) are ignored.
Everything runs in document order after parsing (effectively deferred). See
`doc/gui-scripts.md` for the full design.

### Animation frames (M10)

`requestAnimationFrame` runs a callback once on the next frame; the callback
receives the frame timestamp (ms) and may mutate the DOM, which is restyled and
repainted in the same frame. Re-queue from inside the callback to animate:

```ts
const win = createWindow({ title: "anim", width: 320, height: 240 });
let n = 0;
const tick = (t: number) => {
  win.document.getElementById("label").textContent = String(n++);
  if (n < 120) win.requestAnimationFrame(tick); // id returned; cancelAnimationFrame(id) drops it
};
win.on("ready", () => win.requestAnimationFrame(tick));
win.loadHTML("<div id='label'>0</div>");
run();
```

### Implemented layout (M3)

`runtime/ext_gui/layout.{h,cpp}` turns the styled DOM into a `LayoutBox` tree
with absolute (viewport-relative) geometry:

- **Block flow** — block-level children stack vertically (no margin collapsing
  yet); `display: none` generates no box; `width: auto` fills the containing
  block, `height: auto` wraps the content. The box model (margin/padding/border)
  is resolved, including percentages against the containing block width.
- **Inline flow** — consecutive inline-level children form an anonymous inline
  formatting context with greedy, word-based line breaking, `text-align` and
  `line-height`. Inline elements get the union of their descendants' geometry;
  `display: inline-block` is laid out atomically with a shrink-to-fit width.
  Each text fragment remembers the run it covers, so paint can shape it. Text is
  measured with the HarfBuzz/FreeType stack (see *Implemented text*).
- **Flexbox** — single-line `row`/`column` (and the `-reverse` variants) with
  `gap`, `flex-basis`/`flex-grow`/`flex-shrink`, `justify-content` and
  `align-items` (including `stretch` when the cross size is definite).

Not yet implemented: margin collapsing, multi-line flex wrapping, `position`
offsets (`relative`/`absolute`/`fixed`), `overflow` clipping and floats.

**Document** (`runtime/ext_gui/document.{h,cpp}`): owns the DOM tree, gathers
`<style>` text into one stylesheet, computes styles and layout for a viewport and
offers `querySelector`/`querySelectorAll`/`styleOf`/`boxOf`.

### Implemented paint (M4a/M4b)

`runtime/ext_gui/paint.{h,cpp}` walks the layout tree in painter's order and
emits a backend-agnostic `DisplayList` with two parallel lists: **rectangles**
(backgrounds and four solid border edges, with `border-radius`) and **text runs**
(each carrying its text, colour and resolved `FontSpec`). Keeping them separate
lets the renderer draw all rectangles, then all text on top, with one draw call
per list. `DisplayList::dump()` feeds `paintList()`/`paintCount()`.

`runtime/ext_gui/renderer.{h,cpp}` turns that list into two batched vertex
buffers per window (one for shapes, one for glyph quads) drawn through two
SDL_GPU graphics pipelines:

- The shared pipelines are created lazily from the device's supported shader
  format. Each of SDL_GPU's three backends consumes a different binary and none
  of them compiles GLSL/HLSL at runtime, so the engine ships all three and
  `selectShader` (`renderer_shaders.h`) picks the one the device accepts:
  **MSL** for Metal (compiled by SDL from the embedded source),
  **SPIR-V** for Vulkan (`runtime/ext_gui/spirv/*.{vert,frag}`, compiled by
  `glslc`), and **DXIL** for Direct3D 12 (`shaders.hlsl`, compiled by `dxc`).
  `scripts/build-gui-shaders.mjs` regenerates the embedded blobs and the result
  is committed, so nothing extra is needed to build the engine.
  The descriptor bindings differ per format — SPIR-V follows what
  `SDL_gpu_vulkan.c` builds (uniforms `set=1, binding=0`, samplers
  `set=2, binding=0`, textures `set=2, binding=1`) — and glslc names every entry
  point `main` where MSL/DXIL keep the descriptive names.
  Note that SDL 3.2.10's Direct3D 12 backend cannot create a graphics pipeline
  whose shaders declare a uniform buffer (it fails with `E_INVALIDARG`); the
  engine passes the viewport that way, so the DXIL path is built but not usable
  until that is resolved upstream.
- A **rounded-rectangle distance field** in the shape fragment shader gives
  antialiased fills; the vertex carries `position`, `local`, `half extents`,
  `radius` and colour, and a viewport-size push constant does the projection.
  Alpha blending is enabled.
- The text pipeline samples a **single shared grayscale glyph atlas**
  (`R8_UNORM`, 2048², shelf-packed, LINEAR filtering) and draws each glyph as a
  textured quad (`position`, `uv`, colour), modulating alpha by the coverage.
- Geometry is uploaded only when the document or viewport changes
  (`geometry.dirty`), so steady-state frames are bind-and-draw.

The window background (`setBackground`) is the render-pass clear colour.

Still to do in M4: gradients.

### Implemented input (M5)

`LayoutTree::hitTest` returns the deepest box containing a point (probing later
siblings first so the topmost element wins), and `xt_dom_describe` turns the
element into the `div#id.class` descriptor carried by event payloads.

- SDL pointer/wheel/key events are routed to the owning window, hit tested, and
delivered to the handlers registered with `on`. `mousedown` also moves focus.
- `:hover` matches the hovered element **and its ancestors** (so hovering a child
  lights up its parents); `:focus` matches the focused element. When either
  changes, `XtDocument::setHover`/`setFocus` recompute styles and layout and mark
  the window's geometry dirty, so the change is painted on the next frame.
- `win.hitTest(x, y)` and `win.sendEvent(type, options)` expose hit testing and
  synthetic input so the whole path is testable headlessly (the e2e suite drives
  clicks, wheels and keys without a real mouse).

Still to do for full input: text selection, drag, IME and clipboard.

### Implemented images (M6a)

`runtime/ext_gui/image.{h,cpp}` wraps the vendored **stb_image** header
decoding PNG/JPEG/BMP/GIF/TGA to RGBA8, with a decode cache and a header-only
size cache (`stbi_info`). Paths accept a `file://` prefix and percent-encoding.

- `<img>` is a replaced element (`display: inline-block`): layout gives it the
  CSS size when set, otherwise the intrinsic pixel size, and preserves the
  aspect ratio when only one axis is constrained. The intrinsic size is read
  from the file header (no full decode) while building the box tree.
- Paint emits one `PaintImage` per `<img>`; the renderer decodes/uploads each
  unique `src` to an RGBA texture (cached by path) and draws textured quads,
  batching consecutive quads that share a texture.

Still to do for images: CSS `background-image: url(...)`, `data:` URIs,
`object-fit` and 9-slice borders.

### Implemented transitions (M6b)

`XtDocument` runs CSS transitions between the *target* computed styles (the
cascade for the current `:hover`/`:focus` state) and the *displayed* styles used
for layout and paint. When a state change alters a transitioned property, a
running transition is recorded and re-applied every frame until it finishes;
the engine advances the clock with real frame deltas (`XtDocument::advance`),
and `win.advance(ms)` lets tests step it deterministically.

- Supported properties: `background-color`, `color`, `border-color`,
  `border-radius`; `transition: all` covers them. Structural/layout-affecting
  properties are not animated yet (that would relayout every frame).
- Both the `transition` shorthand and the `transition-property` / `-duration` /
  `-delay` / `-timing-function` longhands are parsed; time values accept `s` and
  `ms`; timing functions are `linear`, `ease`, `ease-in`, `ease-out` and
  `ease-in-out` (`ease` is a smoothstep approximation).
- Retargeting mid-flight starts a new transition from the current interpolated
  value, so reversing a hover animates smoothly from wherever it was.
- `computedStyle()` reports the displayed (interpolated) value, so transitions
  are directly observable in tests.

Still to do for animation: `@keyframes` animations and `cubic-bezier(...)`.

### Implemented text stack (M4b)

`runtime/ext_gui/text.{h,cpp}` wraps **HarfBuzz** (shaping) and **FreeType**
(metrics + eventual rasterisation). Both are built statically and linked into
`gui.a` by `scripts/build-gui.ts`.

- Fonts are resolved from well-known system paths (Helvetica/Arial on macOS,
  DejaVu/Liberation on Linux, Segoe UI/Arial on Windows), overridable with
  `XT_GUI_FONT` (and `XT_GUI_FONT_MONO`), and cached per `(family class, size)`.
  Only regular upright faces are used for now; weight/italic selection is a
  later refinement.
- `xt_text_measure_width` shapes the run with HarfBuzz (so kerning and
  ligatures are honoured), `xt_text_metrics` returns FreeType's ascent /
  descent / normal line height. When no font file can be found the module falls
  back to a deterministic per-byte approximation, so layout still works.
- `xt_text_shape_run` returns positioned glyphs and `xt_text_rasterize` renders
  an 8-bit bitmap; the renderer packs those into the atlas. On HiDPI displays
  glyphs are rasterised at `font_size * SDL_GetWindowPixelDensity` while quads
  are positioned in logical pixels, so text stays crisp.
- Layout uses these real metrics for text widths, line breaking and
  `line-height: normal`; `measureText`/`fontMetrics` expose them to tests.

Multiple windows fall out of the object model: `createWindow` returns a native
object handle; each handle owns its own `SDL_Window`/GPU surface and its own DOM
tree. `run()` drives one shared main loop that ticks every window and exits when
the last one closes.

## Event loop integration

The runtime change (already landed):

- `int xt_loop_poll(int timeout_ms)` — one reactor iteration; `0` polls without
  blocking, `< 0` blocks.
- `void xt_loop_set_main(xt_main_loop_fn fn)` — a host may take over the main
  loop. `xt_run_event_loop()` delegates to it; the generated `main` is unchanged.
- `xt_loop_set_main(NULL)` restores the default `select(2)` loop.

The GUI engine can either register its own loop through `xt_loop_set_main`, or
expose an explicit `run()`; **M2 uses the explicit `run()`** so the window is a
plain native call the TypeScript program controls. Each tick it:

1. pumps SDL window/input events for every window,
2. builds/uploads the display list when it changed and renders every open
   window (clear pass + shape geometry + text geometry),
3. calls `xt_loop_poll(0)` and `xt_drain_microtasks()` so sockets/timers and
   `await` continuations keep making progress,
4. repeats until all windows close or `quit()` is called.

The `xt_loop_set_main` hook remains available for hosts that want to own the
loop themselves.

This keeps network I/O, timers and `await` working inside a GUI program.

## Native ↔ TS bridge

- **TS → engine**: direct `xt_gui_*` calls / window methods.
- **Engine → TS**: `xt_call_with_this(fn, thisValue, argc, argv)` with function
  values captured from TS (e.g. event handlers registered with `win.on(...)`).
- **Future page→native RPC**: not needed while there is no page JS; native TS is
  the controller. If a declarative layer is added later, it will use the same
  `xt_call_*` entry points.

## Packaging & build

- Sources live in `runtime/ext_gui/` (C/C++) plus vendored libs under `vendor/`
  (gitignored; fetched on demand).
- `npm run gui` (`scripts/build-gui.ts`) fetches a pinned SDL3 (`SDL3_TAG`,
  default `release-3.2.10`), builds a static SDL3, fetches and builds static
  FreeType (`FREETYPE_VERSION`) and HarfBuzz (`HARFBUZZ_VERSION`), compiles the
  engine and merges everything into `runtime/lib/<os>-<arch>/gui.a` (or
  `gui.lib` with the MSVC ABI on Windows), same convention as `core.a`,
  statically bundling all three. The merge uses `libtool` on macOS and
  `ar -M` (GNU/LLVM) elsewhere; CMake archives are discovered under the build
  root or `Release/` for either generator style.
- `src/extensions/gui/index.ts` exposes the archive through `nativeObjects()`
  (via `findRuntimeLibrary`, so `.a`/`.lib` both work) and the OS frameworks
  through `linkerFlags()`.
- CI builds `gui.a` before assembling the release archive so it ships inside
  `runtime/lib/<slug>/` (the release tarball copies the whole `runtime/` tree).
  The archive is built and the example is run on Linux (under Xvfb, with the
  lavapipe software Vulkan driver) and macOS; Windows is provisional.
- The existing cache fingerprint already hashes `nativeObjects()` contents, so
  rebuilding `gui.a` invalidates cached binaries automatically.

### Running the example

```sh
npm run runtime      # rebuild core.a after a runtime change
npm run gui          # build runtime/lib/<os>-<arch>/gui.a (fetches SDL3 once)
xbintsc run examples/gui/hello.ts --ext gui
```

Set `XT_GUI_AUTOCLOSE_MS=<n>` to close all windows after `n` milliseconds,
which the e2e test (`tests/e2e/gui-*.test.ts`) uses to run headlessly.

On a headless Linux box, install the SDL3 build headers and run under Xvfb with a
software Vulkan driver:

```sh
sudo apt-get install -y clang cmake libx11-dev libxext-dev libxrandr-dev \
  libxcursor-dev libxi-dev libxinerama-dev libxfixes-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols libdecor-0-dev libasound2-dev libpulse-dev \
  libdbus-1-dev libudev-dev libdrm-dev libgbm-dev libgl1-mesa-dev \
  libegl1-mesa-dev libvulkan-dev mesa-vulkan-drivers xvfb
npm run runtime && npm run gui
xvfb-run -a --server-args="-screen 0 1280x720x24" \
  npx tsx src/cli/main.ts run examples/gui/hello.ts --ext gui
```

Those are the packages the CI `compile-examples` job installs. The X11 backend is
used by default; Wayland is enabled too but not yet exercised.

## Milestones

1. **M1 — foundation** ✅
   - Generic `xt_loop_poll` / `xt_loop_set_main` in the runtime.
   - `gui` extension skeleton + generic CLI extension registration.
2. **M2 — window + GPU clear** ✅
   - SDL3 window, SDL_GPU swapchain, multiple windows, main-loop integration.
   - `createWindow` / `run` / `quit` + `on`/`off` window methods work
     end-to-end (`runtime/ext_gui/`, `scripts/build-gui.ts`).
3. **M3 — HTML/CSS subset** ✅
   - **M3a — parse + cascade** ✅ HTML parser, DOM tree, CSS parser, selector
     matching, UA/author/inline cascade, inheritance, computed style
     (`dom.*`, `css.*`, `style.*`, `document.*`).
   - **M3b — layout** ✅ block/inline flow + Flexbox (`layout.*`),
     `getBoundingClientRect`/`layoutTree`.
4. **M4 — paint + text + display list**
   - **M4a — display list + GPU shapes** ✅ background/border display list,
     rounded-rect SDL_GPU pipeline (`paint.*`, `renderer.*`).
   - **M4b-1 — text stack + metrics** ✅ HarfBuzz + FreeType linked into
     `gui.a`, font resolution/caching, shaping-based text metrics used by
     layout (`text.*`, `measureText`/`fontMetrics`).
   - **M4b-2 — glyph rendering** ✅ FreeType rasterisation, a shared shelf-packed
     glyph atlas, textured text quads in the display list and HiDPI-aware raster
     scaling. Gradients remain.
5. **M5 — input + events** ✅
   - hit testing (`LayoutTree::hitTest`), pointer/wheel/keyboard events delivered
     to TS handlers with a payload, `:hover`/`:focus` stateful matching and
     restyle (`css.*`, `style.*`, `document.*`, `gui.cpp`), plus the
     `hitTest`/`sendEvent` test hooks.
6. **M6 — images, then CSS transitions/animations**
   - **M6a — images** ✅ stb_image decode, `<img>` replaced-element layout,
     per-file GPU textures and textured quads (`image.*`, `paint.*`, `renderer.*`).
   - **M6b — transitions/animations** ✅ `transition` shorthand + longhands,
     animated `background-color`/`color`/`border-color`/`border-radius`, retargeting
     and `win.advance(ms)`. `@keyframes` remain.
7. **M7 — CI & releases** ✅ (Linux/macOS build) / 🚧 (run + Windows)
   - `compile-examples` builds `gui.a` on Linux and macOS (required), then runs
     the example and `tests/e2e/gui-*.test.ts` under Xvfb + lavapipe on Linux
     (required). The macOS run is provisional until a WindowServer is confirmed;
     it is guarded so a failure is logged without annotating the run.
   - The `package` job builds `gui.a` before assembling the release, so it ships
     inside the existing runtime archive (`package-release` copies all of
     `runtime/`).
   - `vendor/` (SDL3/FreeType/HarfBuzz, the slow part) is cached per OS/arch,
     keyed by `scripts/build-gui.ts`.
   - Windows (build + run) stays provisional until the MSVC-compatible
     `gui.lib` and D3D12/DXIL shader path are validated; a failure is logged
     without annotating the run (see *Open questions*).
8. **M8 — interactive DOM** ✅
   - Element/document handles with stable identity, read/write properties via
     runtime accessors, traversal/attributes/queries, mutation with lazy
     restyle/relayout (`dom_api.*`, `document.*`, `dom.*`, `gui.cpp`,
     `window.cpp`), element events with capture + bubble phases and
     `stopPropagation`, and window-level `e.target` compatibility. No compiler
     changes. See `doc/gui-scripts.md`.
9. **M9 — AOT `<script>`** ✅
   - **M9a** ✅ — generic `Extension.assetLoaders` hook + bundler integration.
   - **M9b** ✅ — gui `.html` asset loader (`src/extensions/gui/html.ts`): inline
     bodies are wrapped in `__xt_script_<hash>(window, document)` functions,
     registered through the `__registerScript` builtin and replaced by
     `<script data-xt-id="<hash>">` markers; `win.loadHTML` runs the matching
     functions after parsing and fires `DOMContentLoaded` then `load`.
   - **M9c** ✅ — `<script src>` files are read relative to the HTML, their
     top-level imports are hoisted (specifiers rewritten from the HTML dir) and
     their body is wrapped/registered; missing files and import-binding
     collisions become diagnostics. Everything runs in document order.
10. **M10 — polish** ✅
    - `win.requestAnimationFrame(fn)` / `win.cancelAnimationFrame(id)`; callbacks
      run at the top of each frame with the frame timestamp and may mutate the
      DOM (`gui.cpp`, `window.cpp`, `gui_engine.h`).
    - `Element.offsetWidth` / `offsetHeight` (rounded border box, flushes pending
      mutations) and `Element.contains(other)` (`dom_api.cpp`).

## Progress log

- **M1** ✅ generic `xt_loop_poll`/`xt_loop_set_main`; `gui` extension skeleton.
- **M2** ✅ SDL3 window + SDL_GPU clear, multiple windows, `createWindow`/`run`.
- **M3a** ✅ HTML parser (`dom.*`), CSS parser/matcher (`css.*`), cascade and
  computed style (`style.*`), document model (`document.*`),
  `computedStyle`/`queryCount`/`documentTree`, e2e coverage.
- **M3b** ✅ layout (`layout.*`): block flow, inline formatting context with line
  breaking, single-line Flexbox, `getBoundingClientRect`/`layoutTree`, e2e
  coverage.
- **M4a** ✅ display list (`paint.*`) and the SDL_GPU 2D renderer with an MSL
  rounded-rect pipeline (`renderer.*`), `paintList`/`paintCount`, e2e coverage.
- **M4b-1** ✅ FreeType + HarfBuzz fetched/built/merged into `gui.a`, the text
  module (`text.*`) with font resolution, HarfBuzz shaping and FreeType metrics,
  real text metrics in layout, `measureText`/`fontMetrics`, e2e coverage.
- **M4b-2** ✅ glyph atlas + textured text pipeline in `renderer.*`, shaped text
  runs in `paint.*`, `xt_text_shape_run`/`xt_text_rasterize` in `text.*`, HiDPI
  raster scaling, e2e coverage.
- **M5** ✅ hit testing + input events (`LayoutTree::hitTest`, `xt_dom_describe`,
  `xt_gui_dispatch_*`), `:hover`/`:focus` in the matcher with dynamic restyle,
  `hitTest`/`sendEvent` test hooks, e2e coverage.
- **M6a** ✅ image decoding (`image.*`, vendored stb_image), `<img>` intrinsic
  sizing in layout, `PaintImage` in the display list, per-file RGBA textures and
  an image pipeline in `renderer.*`, e2e coverage.
- **M6b** ✅ CSS transitions (parsing in `style.*`, an animation clock and
  transition state in `document.*`, `advance`/`advance(ms)` hooks, e2e coverage).
- **M7** ✅ builds `gui.a` in CI on Linux and macOS (required) and runs the
  example plus the GUI e2e suite under Xvfb on Linux; `package` ships `gui.a`,
  `vendor/` is cached, and the `ar -M` merge works on GNU/Linux and macOS.
  macOS runs and all of Windows remain provisional in CI.
- **M8** ✅ element/document handles (`dom_api.*`), DOM mutation with lazy
  restyle/relayout (`document.*`, `xt_gui_flush_dom`), element event dispatch
  with capture/bubble and `stopPropagation` (`dom_api.*`, `gui.cpp`), and e2e
  coverage in `tests/e2e/gui-*.test.ts`.
- **M9a** ✅ extensions can register asset loaders keyed by file extension;
  `bundleModules`/`loadGraph` consult them after reading a file. Unit tests in
  `tests/driver/modules.test.ts`.
- **M9b** ✅ `import page from "./page.html"` compiles inline `<script lang="ts">`
  bodies into AOT functions registered at startup and run by `win.loadHTML`
  (before first layout; `DOMContentLoaded` then `load`). Unit tests in
  `tests/extensions/gui.test.ts`, e2e in `tests/e2e/gui-*.test.ts`.
- **M9c** ✅ external `<script src>` files are read, their imports hoisted
  (specifiers rewritten from the HTML dir) and their body wrapped/registered;
  missing files and import-binding collisions are diagnostics. Loader errors are
  caught by `loadGraph` and reported as build errors.
- **M10** ✅ `requestAnimationFrame`/`cancelAnimationFrame` on window handles
  (callbacks run with the frame timestamp before layout each frame) plus
  `offsetWidth`/`offsetHeight`/`contains` on element handles; e2e coverage in
  `tests/e2e/gui-*.test.ts`.

## Open questions

- Whether Linux ships X11, Wayland, or both in the first cut. (Decision:
  X11 first, Wayland later.)
- Windows: an MSVC-compatible `gui.lib` (COFF objects + `ar -M`/`llvm-ar`) is
  produced by `scripts/build-gui.ts`, but it has not been validated in CI yet,
  so the Windows step is provisional (the job stays green) and `package` skips
  it. It also needs an SDL3 build with the D3D12/DXIL backend (DXIL requires
  `dxc`).
- **D3D12 shaders:** the SPIR-V (Vulkan) and DXIL (Direct3D 12) blobs are built
  and embedded, so the non-Metal path no longer skips geometry. DXIL remains
  unusable on **SDL 3.2.10**: its D3D12 backend rejects any graphics pipeline
  whose shaders declare a uniform buffer, and the engine passes the viewport that
  way. Re-check after an SDL upgrade.
