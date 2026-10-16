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

## Open questions

- Whether Linux ships X11, Wayland, or both in the first cut. (Decision:
  X11 first, Wayland later.)
- **SVG:** SVG *files* are supported through the image loader
  (`<img src="logo.svg">`): `runtime/ext_gui/svg.cpp` wraps the vendored
  **NanoSVG** parser/rasteriser and `image.{h,cpp}` treats an SVG as a vector
  image, rasterised at the drawn pixel size. Inline `<svg>` **trees** in a
  document are still not supported: the HTML parser knows elements and
  attributes, the layout engine knows boxes, and paint knows
  rectangles/text/images, so `<svg>` subtrees would need a whole second
  geometry+paint path. Vector art therefore has to arrive as an SVG/PNG file or
  be drawn from boxes, as `examples/gui/pelican-bike` does.
- **Windows: the C++ runtime must not be named on the link line.** `gui.lib` is
  built with the *static* CRT (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`) and
  every engine object records `libcpmt` as a dependent library, which is also
  clang's Windows default and what the xbintsc runtime objects use
  (`scripts/build-gui.ts`). Asking for the DLL runtime as well — the gui
  extension used to pass `-lmsvcprt` — makes the two copies of the same symbols
  collide: `LNK2038: mismatch detected for 'RuntimeLibrary'` plus a wall of
  `LNK2005: already defined`, surfaced as `clang: linker command failed with exit
  code 1169`. `src/extensions/gui/index.ts` now lists only the OS libraries
  (`user32`, `gdi32`, `d3d12`, …). A Windows link is also the only place the
  MSVC environment matters, so build from an x64 Native Tools prompt.
- Windows: an MSVC-compatible `gui.lib` (COFF objects + `ar -M`/`llvm-ar`) is
  produced by `scripts/build-gui.ts` but is not validated in CI, so the Windows
  step is provisional (the job stays green) and `package` skips it. Building and
  running `examples/gui/pelican-svg` and `examples/gui/pelican-bike` on a
  developer machine works. It also needs an SDL3 build with the D3D12/DXIL
  backend (DXIL requires `dxc`).
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
  way. Re-check after an SDL upgrade.
