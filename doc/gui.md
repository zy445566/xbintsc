# xbintsc GUI extension (self-hosted HTML/CSS renderer)

Status: **M3a complete** — HTML parsing, CSS selector matching, the cascade and
computed styles are in place (M2 already opened a GPU-backed SDL3 window with
lifecycle events). Layout and painting are the next steps. This document records
the locked decisions, the architecture, the milestone plan and the current
progress of a cross-platform GUI extension that renders an HTML/CSS UI with its
own GPU-accelerated engine.

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
`ready` (after the first presented frame), `load` and `close`.

### Diagnostics (until paint lands in M4)

So the HTML/CSS pipeline is testable before there is a renderer, a window
handle exposes three read-only hooks:

```ts
win.computedStyle(selector, property)  // e.g. ("#main", "width") -> "60%"
win.queryCount(selector)               // number of matching elements
win.documentTree()                     // serialized DOM (debugging)
```

They are used by `tests/e2e/gui.test.ts` to assert parsing, selector matching,
specificity, inheritance and `!important`. They will stay useful afterwards for
debugging.

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
`:root`, `:not(...)`, `:nth-child(an+b)`, `:disabled`, `:checked`. Values:
lengths (`px`, `%`, `em`, `rem`, `vw`, `vh`, `pt`, `pc`, `in`, `cm`, `mm`, `q`),
colors (hex, `rgb()`/`rgba()`, a named subset), numbers, keywords and shorthands
(`margin`/`padding`/`border`/`flex`).

**Cascade & computed style** (`runtime/ext_gui/style.{h,cpp}`): a small built-in
UA stylesheet, author rules sorted by `(!important, specificity, source order)`,
then inline `style=""` (highest specificity, but non-`!important` inline loses to
`!important`), plus CSS inheritance of the text properties. Relative lengths are
kept unresolved until layout, except `font-size` (resolved against the *parent*
font size) and `line-height`.

**Document** (`runtime/ext_gui/document.{h,cpp}`): owns the DOM tree, gathers
`<style>` text into one stylesheet, computes styles for a viewport and offers
`querySelector`/`querySelectorAll`/`styleOf`.

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
2. renders every open window (a clear pass for now),
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
  default `release-3.2.10`), builds `libSDL3.a`, compiles the engine and merges
  everything into `runtime/lib/<os>-<arch>/gui.a` (same convention as `core.a`),
  statically bundling SDL3.
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
3. **M3 — HTML/CSS subset**
   - **M3a — parse + cascade** ✅ HTML parser, DOM tree, CSS parser, selector
     matching, UA/author/inline cascade, inheritance, computed style
     (`dom.*`, `css.*`, `style.*`, `document.*`).
   - **M3b — layout** block/inline flow + Flexbox (to do).
4. **M4 — paint + text + display list**
   - display list, rounded rects/gradients/borders, HarfBuzz+FreeType text.
5. **M5 — input + events**
   - hit testing, `:hover`/`:focus`, click/scroll/keyboard → TS handlers.
6. **M6 — images, then CSS transitions/animations.**
7. **M7 — CI builds `gui.a` per platform and attaches it to releases.**

## Progress log

- **M1** ✅ generic `xt_loop_poll`/`xt_loop_set_main`; `gui` extension skeleton.
- **M2** ✅ SDL3 window + SDL_GPU clear, multiple windows, `createWindow`/`run`.
- **M3a** ✅ HTML parser (`dom.*`), CSS parser/matcher (`css.*`), cascade and
  computed style (`style.*`), document model (`document.*`),
  `computedStyle`/`queryCount`/`documentTree`, e2e coverage.
- **M3b** ⏳ layout (block/inline flow + Flexbox).

## Open questions

- Whether Linux ships X11, Wayland, or both in the first cut. (Decision:
  X11 first, Wayland later.)
- Windows: needs an MSVC-compatible `.lib` and a D3D12/DXIL SDL3 build; the
  build script currently stops with a clear message there.
