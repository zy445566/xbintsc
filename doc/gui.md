# xbintsc GUI extension (self-hosted HTML/CSS renderer)

Status: **design / early implementation**. This document records the locked
decisions, the architecture, and the milestone plan for a cross-platform GUI
extension that renders an HTML/CSS UI with its own GPU-accelerated engine.

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

## Architecture

```
              TypeScript (compiled by xbintsc to native code)
                │  import { createWindow, run } from "gui"
                ▼
        gui extension bindings  (src/extensions/gui)
                │  xt_gui_* symbols  (uniform (argc, argv) ABI)
                ▼
             libgui.a                ── the self-hosted engine (C/C++)
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

## Proposed stack (needs confirmation)

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

## TS-facing API (first cut)

```ts
import { createWindow, run, quit } from "gui";

const win = createWindow({ title: "Demo", width: 900, height: 600 });
win.loadHTML(INDEX_HTML);          // string or embedded asset
win.on("ready", () => win.eval(`document.body.textContent = "hi"`));

run();                             // drives the main loop until all windows close
```

Multiple windows fall out of the object model: `createWindow` returns a native
object handle; each handle owns its own `SDL_Window`/GPU surface and DOM tree.
`run()` starts one shared main loop that ticks every window.

## Event loop integration

The runtime change (already landed):

- `int xt_loop_poll(int timeout_ms)` — one reactor iteration; `0` polls without
  blocking, `< 0` blocks.
- `void xt_loop_set_main(xt_main_loop_fn fn)` — a host may take over the main
  loop. `xt_run_event_loop()` delegates to it; the generated `main` is unchanged.
- `xt_loop_set_main(NULL)` restores the default `select(2)` loop.

The GUI engine registers its own loop. Each tick it:

1. pumps SDL window/input events for every window,
2. calls `xt_loop_poll(0)` to service sockets/timers and drain microtasks,
3. renders dirty windows,
4. repeats until all windows close or `quit()` is called.

This keeps network I/O, timers and `await` working inside a GUI program.

## Native ↔ TS bridge

- **TS → engine**: direct `xt_gui_*` calls / window methods.
- **Engine → TS**: `xt_call_with_this(fn, thisValue, argc, argv)` with function
  values captured from TS (e.g. event handlers registered with `win.on(...)`).
- **Future page→native RPC**: not needed while there is no page JS; native TS is
  the controller. If a declarative layer is added later, it will use the same
  `xt_call_*` entry points.

## Packaging & build

- Sources live in `runtime/ext_gui/` (C/C++) plus vendored libs under `vendor/`.
- A build script produces `runtime/lib/<os>-<arch>/gui.a` (same convention as
  `core.a`), statically bundling the vendored libs.
- `src/extensions/gui/index.ts` exposes the archive through `nativeObjects()`
  and the OS frameworks through `linkerFlags()`.
- The existing cache fingerprint already hashes `nativeObjects()` contents, so
  rebuilding `gui.a` invalidates cached binaries automatically.

## Milestones

1. **M1 — foundation** (this branch)
   - Generic `xt_loop_poll` / `xt_loop_set_main` in the runtime.
   - `gui` extension skeleton + generic CLI extension registration.
2. **M2 — window + GPU clear**
   - SDL3 window, SDL_GPU swapchain, multi-window, main-loop integration.
   - `createWindow` / `run` / `quit` work end-to-end.
3. **M3 — HTML/CSS subset**
   - HTML parser, selector matching, cascade, block/inline flow + Flexbox.
4. **M4 — paint + text**
   - display list, rounded rects/gradients/borders, HarfBuzz+FreeType text.
5. **M5 — input + events**
   - hit testing, `:hover`/`:focus`, click/scroll/keyboard → TS handlers.
6. **M6 — images, then CSS transitions/animations.**
7. **M7 — CI builds `gui.a` per platform and attaches it to releases.**

## Open questions

- Confirm the **SDL3 + SDL_GPU** stack (Q3 in the design discussion).
- Whether Linux ships X11, Wayland, or both in the first cut.
