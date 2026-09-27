# xbintsc GUI extension (self-hosted HTML/CSS renderer)

Status: **M5 complete** — HTML parsing, CSS selector matching, the cascade,
computed styles and layout (block, inline and Flexbox) are in place, and the
engine *paints*: it builds a display list of rectangles and shaped text runs and
renders them through SDL_GPU. Input events (mouse, wheel, keyboard) are hit
tested and delivered to native TS handlers, and `:hover`/`:focus` are matched
dynamically. This document records the locked decisions, the architecture, the
milestone plan and the current progress of a cross-platform GUI extension that
renders an HTML/CSS UI with its own GPU-accelerated engine.

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

- Executing page `<script>`. Logic lives in **xbintsc-compiled native TS**; the
  HTML/CSS only describes the interface.
- A JS engine (QuickJS/V8/...). Explicitly out of scope.
- Full web compatibility / a browser. We implement a practical HTML/CSS subset.

## Locked decisions

| # | Decision |
| --- | --- |
| 1 | **No page JS engine.** All behaviour is native TS, called back through `xt_call_with_this`. |
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
| Images | **stb_image** (later libpng/libjpeg) | single header to start |

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
```

They are used by `tests/e2e/gui.test.ts` to assert parsing, selector matching,
specificity, inheritance, `!important` and layout geometry. They will stay useful
afterwards for debugging.

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
  format. On macOS the renderer compiles the embedded **MSL** source directly
  (SDL_GPU `SHADERFORMAT_MSL`); Vulkan/D3D12 need SPIR-V/DXIL blobs and are a
  build-time TODO (see *Open questions*).
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
  default `release-3.2.10`), builds `libSDL3.a`, fetches and builds static
  FreeType (`FREETYPE_VERSION`) and HarfBuzz (`HARFBUZZ_VERSION`), compiles the
  engine and merges everything into `runtime/lib/<os>-<arch>/gui.a` (same
  convention as `core.a`), statically bundling all three.
- `src/extensions/gui/index.ts` exposes the archive through `nativeObjects()`
  and the OS frameworks through `linkerFlags()`.
- The existing cache fingerprint already hashes `nativeObjects()` contents, so
  rebuilding `gui.a` invalidates cached binaries automatically.

### Running the example

```sh
npm run runtime      # rebuild core.a after a runtime change
npm run gui          # build runtime/lib/<os>-<arch>/gui.a (fetches SDL3 once)
xbintsc run examples/gui/hello.ts --ext gui
```

Set `XT_GUI_AUTOCLOSE_MS=<n>` to close all windows after `n` milliseconds,
which the e2e test (`tests/e2e/gui.test.ts`) uses to run headlessly.

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
6. **M6 — images, then CSS transitions/animations.**
7. **M7 — CI builds `gui.a` per platform and attaches it to releases.**

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

## Open questions

- Whether Linux ships X11, Wayland, or both in the first cut. (Decision:
  X11 first, Wayland later.)
- Windows: needs an MSVC-compatible `.lib` and a D3D12/DXIL SDL3 build; the
  build script currently stops with a clear message there.
- **Shaders on non-Metal backends:** the renderer embeds MSL source (compiled by
  SDL_GPU at runtime on macOS). Vulkan needs SPIR-V and D3D12 needs DXIL; those
  require `glslc`/`dxc` at build time. Until then the non-Metal path clears the
  window and skips geometry (logged once).
