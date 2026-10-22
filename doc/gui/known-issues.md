# Known issues and open questions

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

## Known issues

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
  produced by `scripts/build-gui.ts` but is not validated in CI, so the Windows
  step is provisional (the job stays green) and `package` skips it. Building and
  running `examples/gui/pelican-svg` and `examples/gui/pelican-bike` on a
  developer machine works. SDL_GPU selects the **Vulkan** backend there
  (`win.driver()` reports `"vulkan"`), so the archive needs no DXIL/`dxc`
  toolchain; the hosted Windows runners have no GPU/Vulkan driver, which is why
  the CI run stays provisional.
- **Media on Windows:** the media decoders (FFmpeg) are only built and merged
  into the POSIX `gui.a`; the Windows `gui.lib` is produced without them, so
  `<video>`/`<audio>` are inert there (failures fire `error`). Enabling it needs
  an MSVC-compatible FFmpeg build and is deferred until the Windows GUI path
  itself is validated. `<video controls>` is likewise deferred — the intended
  pattern is TS + a CSS overlay, as in `examples/gui/media.ts`.
- **D3D12 shaders:** the SPIR-V (Vulkan) and DXIL (Direct3D 12) blobs are built
  and embedded, so the non-Metal path no longer skips geometry. DXIL remains
  unusable on **SDL 3.2.10**: its D3D12 backend rejects any graphics pipeline
  whose shaders declare a uniform buffer, and the engine passes the viewport that
  way. This does not affect Windows in practice because SDL_GPU selects Vulkan
  there; re-check after an SDL upgrade if the D3D12 path is ever forced.
