# Known issues and open questions

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

## Known issues

- **WebGL 1.0 and 2.0 run on the GPU, with a subset of each.**
  `canvas.getContext("webgl")` / `"webgl2"` return real GPU contexts (see
  [WebGL](./webgl.md)): GLSL ES shaders are translated to SPIR-V by the bundled
  glslang — consumed directly by the Vulkan backend, or cross-compiled to MSL by
  the bundled SPIRV-Cross for Metal — and the drawing buffer is an offscreen
  colour texture that the page composites without a readback. WebGL 2 adds VAOs,
  instancing, integer attributes, sampler objects, `texStorage2D`, buffer copies,
  uniform blocks, cube/3D/array textures, float/depth/compressed textures,
  multiple render targets and scissored clears. `blitFramebuffer` copies colour
  and depth; transform feedback, MSAA and stencil blits are not implemented
  (SDL_GPU has no transform-feedback stage, its Metal multisample resolve is
  unreliable, and a stencil attachment is not sampleable). On an SDL_GPU backend
  that accepts neither SPIR-V nor MSL (Direct3D 12, which would need DXIL) both
  context kinds are `null`. The `.js` asset loader now patches only three.js's
  shader unroller; `WebGLRenderer` runs on the real backend and there is no
  Canvas2D fallback. Shadow maps (comparative `sampler2DShadow` targeting depth
  textures), cube textures/environment maps and HDR render targets are supported.



- **Intermittent crash at auto-close.** `XT_GUI_AUTOCLOSE_MS` can end a run
  with a native access violation in roughly two runs out of five; a clean exit
  looks identical otherwise. Windows reports it as the NTSTATUS exit code
  `0xC0000005`; on Linux/macOS the process dies with a signal — an access
  violation is `SIGSEGV`, so `spawnSync` reports `status: null` plus a
  `signal`. It only reproduces with a *running animation-frame loop* (a static
  document closes fine) and it is **not** the game's own code: no exception is
  reported, the crash usually lands inside the first second regardless of the
  deadline, and raising `XT_GC_THRESHOLD` hides it, so it looks like a
  GC/teardown race. Interactive runs (no auto-close) are stable for minutes.
  A leading cause was that the engine cached `xt_value`s in C++ containers
  the mark-sweep collector cannot see — the AOT `<script>` registry, element
  handles, element listeners and the queued/currently-running
  `requestAnimationFrame` callbacks. They are now marked by GC root providers
  (`xt_gui_script_gc_scan` and `xt_gui_gc_scan_roots`), which removes that
  class of use-after-free. Since the M12 node-lifetime work it has not
  reproduced at all: 44 auto-close runs of `examples/gui/pelican-bike` (deadlines
  800 ms–4 s, with the default, a 1 MiB and a 64 MiB collector threshold, where
  the old report said roughly two in five) all exited cleanly, which points at
  the same root cause — a detached-node pool and handle table that grew with the
  frame count while the collector's threshold ratcheted up. It stays listed
  because the archive is built per platform and the failure is timing-sensitive;
  `tests/e2e/gui-example.test.ts` still treats either crash spelling as an
  acceptable outcome, and the behavioural assertions live in
  `tests/e2e/gui-example-browser.test.ts` (no GPU, deterministic).
- **`display: none -> flex` restyle.** A hidden element being shown by a
  *class change* on a flex container has also been observed to crash the same
  way; `examples/gui/pelican-bike` reveals its game-over overlay with `opacity`
  instead.
- **CSS coverage is a deliberate subset.** Grid, `clamp()`/`min()`/`max()`/`env()`,
  `var()` custom properties, the `inset` shorthand, `box-sizing`, `visibility` and
  `pointer-events`, `@media` queries, generated content (`::before`/`::after`),
  multi-layer linear/radial backgrounds, `text-shadow`, `clip-path`
  (`inset`/`circle`/`ellipse`/`polygon`) and `filter` colour functions are
  implemented. Known limits: `mix-blend-mode`, `background-clip`,
  `background-position`/`size`/`repeat`, repeating gradients and `backdrop-filter`
  compositing are not implemented (`backdrop-filter` parses and cascades, but
  sampling the backdrop needs an offscreen layer, so it paints nothing);
  `filter: blur()` is a nine-tap per-shape spread rather than a true group blur,
  so overlapping descendants can darken; `drop-shadow` is approximated from box
  silhouettes; at most two nested `clip-path` shapes are intersected (deeper
  nesting keeps the outer two); and images/canvases are not colour-filtered
  except for the brightness/opacity tint the image shader can carry. Canvas
  textures are sampled nearest-neighbour. Background gradients remain solid
  bands (18 for linear, 32 concentric ellipses for radial), so a soft gradient
  may show slight banding.
- **SVG is NanoSVG's subset.** SVG *files* (`<img src="logo.svg">`) and inline
  `<svg>` trees share the vendored NanoSVG parser/rasteriser
  (`runtime/ext_gui/svg.cpp`, `inline_svg.cpp`), which renders
  `path`/`rect`/`circle`/`ellipse`/`line`/`polyline`/`polygon`, groups, `defs`
  gradients and class-based `<style>` rules. `<text>`, `<use>`, filters, masks
  and clip paths are not rendered, and CSS applied by the document cascade does
  not reach inside the artwork (fill/stroke are SVG presentation attributes,
  not CSS properties here). Vector art that needs those has to be rasterised
  beforehand, drawn from boxes, or restricted to the supported syntax. See
  [Media](./media.md#images-m6a).
- **Rendering is still whole-document per change.** A DOM mutation recomputes
  the cascade and lays out the complete document again (there is no incremental
  subtree invalidation), and a change that only affects paint (a hover colour,
  for example) reflows too. Documents with generated content
  (`::before`/`::after`) are not special-cased. The measured cost of each stage
  is exposed by `win.stats()`/`XT_GUI_PERF`, which is what a follow-up would be
  keyed off; see [Performance](./rendering.md#performance-m20).
- **Draw calls follow painter order.** Runs only merge when they are
  consecutive in painter order and share pipeline, clip and texture, so a list
  whose rows alternate a background and text costs one shape run and one text
  run per row (`drawCalls` in `win.stats()`). Reordering for fewer draw calls
  needs an overlap-aware batcher (text may be covered by a later background) and
  is not implemented. Measured: the 200-row perf page issues 400 draw calls and
  spends about 0.4 ms in the draw phase, so this is a follow-up for
  draw-call-dominated pages rather than the current bottleneck.
- **The text shaping cache is bounded.** Two generations of up to 2 MiB of
  glyphs / 8,192 entries hold `(face, text) -> glyphs`. A document with more
  distinct words than that re-shapes some of them after a cache rotation;
  `textCacheBytes`/`textShapeHits` in `win.stats()` show whether it is the
  bottleneck. Inline `style="…"` attributes are re-parsed on each restyle of
  the element that carries them.

## Open questions

- Whether Linux ships X11, Wayland, or both in the first cut. (Decision:
  X11 first, Wayland later.)
- **Windows: the C++ runtime must not be named on the link line.** `gui.lib` is
  built with the *static* CRT (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`) and
  every engine object records `libcpmt` as a dependent library, which is also
  clang's Windows default and what the xbintsc runtime objects use
  (`scripts/build-gui.ts`). Asking for the DLL runtime as well — the gui
  extension used to pass `-lmsvcprt` — makes the two copies of the same symbols
  collide: `LNK2038: mismatch detected for 'RuntimeLibrary'` plus a wall of
  `LNK2005: already defined`, surfaced as `clang: linker command failed with exit
  code 1169`. `src/extensions/gui/index.ts` now lists only the OS libraries
  (`user32`, `gdi32`, `d3d12`, …). `scripts/build-gui.ts` resolves the same
  MSVC-targeting clang as the driver (the LLVM bundled with Visual Studio when
  no newer one is on `PATH`), so a developer prompt is not required.
- Windows: an MSVC-compatible `gui.lib` (COFF objects + `ar -M`/`llvm-ar`) is
  produced by `scripts/build-gui.ts`. The Windows x64 CI job builds it with the
  Visual Studio toolchain (`.github/actions/vs-toolchain` imports `vcvarsall`
  and uses VS's bundled LLVM), links a `gui,media` program, and ships it in the
  x64 release. *Running* the examples on the hosted runner stays provisional
  because it has no GPU/Vulkan driver (SDL_GPU selects the **Vulkan** backend
  there, so no DXIL/`dxc` toolchain is needed). Windows arm64 still skips both
  archives. Building and running `examples/gui/pelican-svg` and
  `examples/gui/pelican-bike` on a developer machine works.
- **Windows: x64 builds in CI, arm64 and runtime validation stay provisional.**
  `media.lib` is produced by `scripts/build-media.ts` with FFmpeg's MSVC
  toolchain (`--toolchain=msvc --cc=clang-cl`) so its COFF objects and static CRT
  match `gui.lib`, and `--ext media` links and plays there. Building it needs a
  POSIX shell to run FFmpeg's `configure` and GNU make; the shell comes from Git
  for Windows (already needed to clone SDL3), and make is a native GNU make
  (Chocolatey's `make`, or `xbintsc_MAKE`). CI installs make explicitly,
  builds `gui.lib` + `media.lib` on Windows x64 (using the same Visual Studio
  `vcvarsall` + bundled LLVM as a local build), links a `gui,media` program, and
  ships both archives in the Windows x64 release; the example *run* remains
  provisional because hosted runners have no GPU/Vulkan driver. Windows arm64
  still skips both archives, and `<video controls>` is likewise deferred — the
  intended pattern is TS + a CSS overlay, as in `examples/gui/media.ts`.
- **D3D12 shaders:** the SPIR-V (Vulkan) and DXIL (Direct3D 12) blobs are built
  and embedded, so the non-Metal path no longer skips geometry. DXIL remains
  unusable on **SDL 3.2.10**: its D3D12 backend rejects any graphics pipeline
  whose shaders declare a uniform buffer, and the engine passes the viewport that
  way. This does not affect Windows in practice because SDL_GPU selects Vulkan
  there; re-check after an SDL upgrade if the D3D12 path is ever forced.