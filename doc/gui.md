# xbintsc GUI extension (self-hosted HTML/CSS renderer)

Status: **M16** — features (M1–M15) are complete: HTML parsing, CSS selector
matching, the cascade, computed styles and layout (block, inline, Flexbox and
CSS positioning) are
in place, and the engine *paints*: it builds a display list of rectangles, images
and shaped text runs and renders them through SDL_GPU. Input events are hit
tested and delivered to native TS handlers, `:hover`/`:focus` are matched
dynamically, `<img>` is sized from its intrinsic dimensions and drawn from a
texture — PNG/JPEG/… bitmaps and SVG files alike, the latter rasterised at the
drawn size so vector art stays crisp — and CSS transitions animate paint
properties. **M16** gives inline `<svg>` trees the same treatment: the subtree is
serialized back into an SVG document and rasterised at the drawn device size, so
an icon needs no separate file. **M8** adds an interactive
DOM: element handles with stable identity, mutation (`appendChild`, `textContent`,
`classList`, `style`, …) and element-level events with capture/bubbling.
**M9** compiles `<script>` bodies ahead of time (inline and `<script src>`) — see
`doc/gui-scripts.md`. **M10** adds `requestAnimationFrame` plus a few DOM helpers.
**M11** adds `position: relative`/`absolute`/`fixed` with their offsets, which is
what `examples/gui/pelican-bike` — a playable 2D game — is built on. **M12** makes
DOM node lifetime explicit (what a mutation releases, what it keeps) and gives the
engine bounded memory: see [Node lifetime](./gui/dom.md#node-lifetime) and
[Memory & GC](./gui/runtime.md#memory--gc). **M13** plays real
`<video>`/`<audio>` — a vendored minimal FFmpeg decodes them on the main thread,
video frames go through the image texture path and audio to an SDL stream, with
the DOM media API exposing `play`/`pause`/`currentTime`/… and media events.
**M14** adds `<canvas>` with a self-written 2D software rasteriser (anti-aliased
fills, paths, text and `drawImage`) uploaded through the image texture path —
see [Canvas](./gui/canvas.md). **M15** adds CSS `transform` /
`transform-origin`: a composited `Matrix2D` per layout box that the display list
carries into the renderer, so boxes (and the text, images and children inside
them) can be translated, scaled and rotated without reflowing anything — and
`transition: transform`, so those poses animate. See
[Layout](./gui/layout.md#transforms-m15) and
[Animation](./gui/animation.md#css-transforms-m15).
This overview records the locked decisions, the architecture, the milestone plan
and the current progress; the engine itself is documented by function in the
pages above.

## Contents

- [API](./gui/api.md) — window handles, events and diagnostics
- [DOM, HTML and CSS](./gui/dom.md) — parsing, cascade, interactive DOM and AOT scripts
- [Layout](./gui/layout.md) — block, inline, Flexbox and positioning
- [Rendering](./gui/rendering.md) — display list, GPU pipelines and text
- [Media](./gui/media.md) — images, SVG, video and audio
- [Canvas](./gui/canvas.md) — `<canvas>` and the 2D context
- [Input](./gui/input.md) — hit testing, events and `:hover`/`:focus`
- [Animation](./gui/animation.md) — `requestAnimationFrame`, CSS transitions and transforms
- [Runtime integration](./gui/runtime.md) — event loop, native bridge and memory
- [Packaging and build](./gui/build.md) — building `gui.a` and running examples
- [Known issues and open questions](./gui/known-issues.md)

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
        images (stb_image; SVG via NanoSVG)
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
| Images | **stb_image** (vendored header) for bitmaps, **NanoSVG** (vendored headers) for SVG | single headers to start; SVG files rasterise on demand |
| Video + audio | **FFmpeg** (statically vendored, minimal build) | decodes the common container/codec set; no WebView or platform media framework |

Pinned versions: SDL3 `release-3.2.10`, FreeType `2.13.3`, HarfBuzz `10.1.0`,
FFmpeg `7.1` (overridable with `SDL3_TAG` / `FREETYPE_VERSION` /
`HARFBUZZ_VERSION` / `FFMPEG_VERSION`).

Alternative if SDL3 is rejected: **GLFW + OpenGL 3.3** (simpler, but OpenGL is
deprecated on macOS and gives no modern GPU abstraction). **wgpu-native** if the
engine were Rust.

> These vendored libraries are linked **statically into `gui.a`** so a released
> xbintsc stays "download and run"; only OS frameworks are added by
> `linkerFlags()`.

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
   - **M6a — images** ✅ stb_image decode (bitmaps) and NanoSVG rasterisation
     (SVG files, at the drawn pixel size), `<img>` replaced-element layout,
     per-file GPU textures and textured quads (`image.*`, `svg.*`, `paint.*`,
     `renderer.*`).
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
   - Windows (build + run) stays provisional in CI: the MSVC-compatible
     `gui.lib` builds and runs locally through SDL_GPU's Vulkan backend, but the
     hosted runners have no GPU/Vulkan driver. A failure is logged without
     annotating the run (see [Open questions](./gui/known-issues.md#open-questions)).
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
11. **M11 — CSS positioning** ✅
    - `position: relative` / `absolute` / `fixed` with `top`/`right`/`bottom`/
      `left`, resolved against the nearest positioned ancestor (the viewport when
      there is none), `auto` insets keeping the static position, shrink-to-fit
      for an out-of-flow `width: auto` and fill-available when both horizontal
      insets are set (`layout_flow.cpp`, `layout.h`).
    - Item margins are honoured in the flex algorithm, so a negative margin
      participates in the layout instead of being dropped (`layout_flex.cpp`).
    - Drives `examples/gui/pelican-bike` and e2e coverage in
      `tests/e2e/gui-layout.test.ts`.
12. **M12 — node lifetime + memory** ✅
    - What a mutation releases and what it keeps is now explicit: the
      `innerHTML`/`inner`/`textContent`/`replaceChild` paths free the subtrees
      they drop (`XtDocument::discard`, `domapi::discard_subtree`), while
      `removeChild`/`remove` keep DOM semantics (`document.*`, `dom_api_*.cpp`).
    - Handle slots are recycled with a per-slot generation, so a stale handle
      stays inert while the table stops growing with churn (`dom_api.cpp`,
      `gui_engine.h`).
    - The collector's trigger follows the live set instead of only ratcheting up,
      so a leaking or spiking program cannot spiral (`runtime/xt_alloc.c`).
      `XT_GC_TRACE=1` reports every collection.
    - `win.stats()` exposes the engine counters; e2e coverage in
      `tests/e2e/gui-retention.test.ts` (see [Memory & GC](./gui/runtime.md#memory--gc)).
13. **M13 — video & audio** ✅
    - `<video>` / `<audio>` decode through a statically vendored minimal FFmpeg
      (`media.*`, `gui_media.cpp`), frames upload to GPU textures through the
      existing image pipeline, audio is pushed to an SDL stream on the main
      thread, and the DOM media API plus `loadedmetadata`/`play`/`pause`/
      `ended`/`timeupdate`/`error` events are exposed on element handles
      (`dom_api_media.cpp`, `dom_api_proto.cpp`). Controlled from TS + CSS;
      there are no built-in controls. Example in `examples/gui/media.*`, e2e in
      `tests/e2e/gui-media.test.ts`.
14. **M14 — `<canvas>` 2D** ✅
    - `<canvas>` is a replaced element (`xtgui::Canvas2D`, `layout.*`), the
      `getContext("2d")` handle and drawing API live in `dom_api_canvas.cpp`,
      the self-written anti-aliased software rasteriser in `canvas_raster.cpp`,
      and the per-window lifetime in `gui_canvas.cpp`. The bitmap uploads to a
      GPU texture through the image path (`renderer_image.cpp`) and the display
      list carries a `canvas_id` (`paint.*`). See [Canvas](./gui/canvas.md);
      e2e in `tests/e2e/gui-canvas.test.ts`.
15. **M15 — CSS `transform`** ✅
    - `transform`/`transform-origin` parse into a `TransformOp` list
      (`transform.{h,cpp}`, `style.*`), transition like any other paint
      property (`document.*`), and are composed into a per-box `world` matrix
      (plus its inverse) after layout (`layout.*`). Paint carries the matrix on
      every op and the renderer maps quad corners through it, so the shaders are
      untouched. Hit testing and `getBoundingClientRect` follow. See
      [Layout](./gui/layout.md#transforms-m15) and
      [Animation](./gui/animation.md#css-transforms-m15); e2e in
      `tests/e2e/gui-transform.test.ts` and `tests/e2e/gui-example-svg.test.ts`.
16. **M16 — inline `<svg>`** ✅
    - `<svg>` is a replaced element (`layout.*`): its intrinsic size comes from
      the `width`/`height` attributes, else the `viewBox`, else 300x150, and the
      normal box walk does not descend into its children.
    - `inline_svg.{h,cpp}` serializes the subtree into a standalone SVG document,
      restoring the case-sensitive names the HTML parser lowercased (`viewBox`,
      `preserveAspectRatio`, `linearGradient`, `gradientUnits`, …).
    - Paint carries the markup on its image op (`PaintImage::svg`) and the
      renderer parses/rasterises it at the drawn device size through the same
      NanoSVG cache as an SVG file (`svg.{h,cpp}`); the markup cache is bounded
      and a DOM mutation re-serializes on the next layout. e2e in
      `tests/e2e/gui-inline-svg.test.ts`.

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
  an image pipeline in `renderer.*`, e2e coverage. SVG files are decoded too
  (`svg.*`, vendored NanoSVG): a vector source is rasterised at the drawn device
  pixel size and cached by that size, so scaling and HiDPI stay crisp.
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
- **M11** ✅ `position: relative`/`absolute`/`fixed` with `top`/`right`/`bottom`/
  `left`. Layout keeps a containing block per box (the nearest positioned
  ancestor's padding box, published only once that ancestor has its final size,
  so `bottom`/`right` are exact), absolute boxes are laid out after in-flow
  content so they never affect it, shrink-to-fit/fill-available width rules are
  applied, and flex items now account for their margins. e2e coverage in
  `tests/e2e/gui-layout.test.ts`; `examples/gui/pelican-bike` is built on it.
- **M12** ✅ DOM node lifetime and memory. `innerHTML`/`textContent`/
  `replaceChild` now release the subtrees they drop instead of parking them in
  the detached pool forever, `removeChild` keeps its documented DOM semantics,
  handle slots are recycled with a per-slot generation so stale handles stay
  inert, and the collector's trigger follows the live set instead of only
  ratcheting upwards. `win.stats()` exposes the engine counters and
  `tests/e2e/gui-retention.test.ts` pins them; a game loop rewriting its sprite
  every frame now runs in constant memory (see [Memory & GC](./gui/runtime.md#memory--gc)).
- **M13** ✅ `<video>` / `<audio>` support. A minimal static FFmpeg is vendored
  and merged into `gui.a` (`scripts/build-gui.ts`), `media.*` decodes video to
  RGBA frames and audio to 48 kHz stereo PCM on the main thread, `gui_media.cpp`
  owns the players and pumps the media clock and events each frame,
  `renderer_image.cpp` uploads each new video frame through the image texture
  path, and `dom_api_media.cpp` exposes `play`/`pause`/`load` plus the media
  properties on element handles. `examples/gui/media.*` demonstrates the
  TS-plus-CSS-controls pattern and `tests/e2e/gui-media.test.ts` pins it.
- **M14** ✅ `<canvas>` support. A new replaced element (`layout.*`) carries an
  intrinsic size from its `width`/`height` attributes (default `300x150`),
  `xtgui::Canvas2D` (`canvas.{h,cpp}`) owns the RGBA8 bitmap and drawing state,
  `canvas_raster.cpp` is a self-written anti-aliased software rasteriser
  (4 sub-scanlines + analytic horizontal coverage, non-zero winding), the
  `getContext("2d")` handle and its API are bound in `dom_api_canvas.cpp`, and
  `gui_canvas.cpp` owns the per-window lifetime. The bitmap uploads through the
  image texture path (`renderer_image.cpp`) and the display list carries a
  `canvas_id` (`paint.*`). The drawing surface was later widened with linear /
  radial gradients (`canvas_gradient.cpp`), `ellipse`/`roundRect`, line
  caps/joins/miter limits and dash patterns (`canvas_style.cpp`,
  `canvas_stroke.cpp`), and `getTransform`; `transform` now post-multiplies, so
  `translate(x, y); rotate(a)` rotates around the translated origin, and
  `examples/gui/pelican-canvas` draws an animated pelican on a bicycle entirely
  with these APIs. See [Canvas](./gui/canvas.md); e2e in
  `tests/e2e/gui-canvas.test.ts`.
- **M15** ✅ CSS `transform` / `transform-origin`. A new translation unit
  (`transform.{h,cpp}`) parses `translate`/`translateX`/`translateY`/`scale`/
  `scaleX`/`scaleY`/`rotate`/`matrix` (plus `translate3d`/`scale3d`, whose `z`
  argument is dropped) into a `Matrix2D` list, reports them back through
  `computedStyle`, and interpolates them for transitions (a shape or unit change
  snaps). Layout composes a `world` matrix and its inverse per box after the
  normal pass (`applyTransforms`), resolving `transform-origin` against the
  border box — so layout geometry, and therefore flow, never changes. Paint
  attaches the matrix to every shape/text/image op and the renderer maps quad
  corners through it (no shader or uniform change), hit testing maps the point
  through the inverse, and `getBoundingClientRect` returns the transformed AABB.
  `examples/gui/pelican-svg` animates its artwork with it; e2e in
  `tests/e2e/gui-transform.test.ts` and `tests/e2e/gui-example-svg.test.ts`.
- **M16** ✅ inline `<svg>`. `buildBoxTree` treats an `<svg>` element as a
  replaced box (intrinsic size from `width`/`height`, then `viewBox`, then
  300x150) and stops descending, `inline_svg.{h,cpp}` serializes the subtree with
  canonical SVG casing, paint carries the markup on `PaintImage::svg`, and
  `renderer_image.cpp` rasterises it at the drawn device pixel size through the
  NanoSVG cache (`svg.{h,cpp}`, bounded LRU for markup). The `win.inlineSvg`
  diagnostic exposes the serialized document, and `tests/e2e/gui-inline-svg.test.ts`
  pins sizing, hit testing, gradients, CSS box styling and mutation.

