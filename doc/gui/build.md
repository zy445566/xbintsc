# Packaging and build

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

- Sources live in `runtime/ext_gui/` (C/C++) plus vendored libs under `vendor/`
  (gitignored; fetched on demand). Media decoding lives in
  `runtime/ext_media/` and is built separately.
- `npm run gui` (`scripts/build-gui.ts`) fetches a pinned SDL3 (`SDL3_TAG`,
  default `release-3.2.10`), builds a static SDL3, fetches and builds static
  FreeType (`FREETYPE_VERSION`) and HarfBuzz (`HARFBUZZ_VERSION`), compiles the
  engine and merges everything into `runtime/lib/<os>-<arch>/gui.a` (or
  `gui.lib` with the MSVC ABI on Windows), same convention as `core.a`,
  statically bundling all three. The merge uses `libtool` on macOS and
  `ar -M` (GNU/LLVM) elsewhere; CMake archives are discovered under the build
  root or `Release/` for either generator style.
- `npm run media` (`scripts/build-media.ts`) builds the optional **FFmpeg**
  backend. It fetches FFmpeg (`FFMPEG_VERSION`, default `7.1`), configures a
  minimal static build with only the demuxers/parsers/decoders the media
  elements need, compiles `runtime/ext_media/media.cpp` against it and merges
  everything into `runtime/lib/<os>-<arch>/media.a`. `gui.a` does **not** link
  FFmpeg: a program enables playback explicitly with `--ext media`.
- `src/extensions/gui/index.ts` exposes the engine archive through
  `nativeObjects()` (via `findRuntimeLibrary`, so `.a`/`.lib` both work) and the
  OS frameworks through `linkerFlags()`. `src/extensions/media/index.ts` does
  the same for `media.a` and compiles the registration bridge
  (`runtime/ext_media/media_bridge.c`) as a normal runtime source.
- `scripts/build-gui.ts` compiles with the *same* clang the driver resolves
  (`src/driver/toolchain-provider.ts`); on Windows that is the MSVC-targeting
  LLVM bundled with Visual Studio, and its sibling `clang++`/`llvm-ar` are used
  for CMake, the engine and the archive merge. There are no `gcc`/`g++`
  fallbacks, and Windows always uses `llvm-ar`, so the archive cannot mix object
  formats or C++ runtimes. The Visual Studio toolchain is found by absolute
  path, so no developer prompt is required. On Windows SDL_GPU selects the
  Vulkan backend; no DXIL/`dxc` toolchain is needed. `scripts/build-media.ts`
  uses the same resolved toolchain for FFmpeg and the player.
- CI builds `gui.a` and `media.a` before assembling the release archive so they
  ship inside `runtime/lib/<slug>/` (the release tarball copies the whole
  `runtime/` tree). The archives are built and the examples are run on Linux
  (under Xvfb, with the lavapipe software Vulkan driver) and macOS; Windows is
  provisional because the hosted runners have no GPU/Vulkan driver, and the
  media archive is POSIX-only.
- The existing cache fingerprint already hashes `nativeObjects()` contents and
  `runtimeSources()` content, so rebuilding either archive invalidates cached
  binaries automatically.
- CMake build trees live *next to* their sources (`vendor/sdl-build`,
  `vendor/freetype-build`, `vendor/harfbuzz-build`), never inside them: a
  `CMakeCache.txt` records the machine it was configured on, and SDL's video
  backends are `option()`s, so the first configure to run wins. A cached
  `vendor/SDL` tree that also carried `build/` therefore restored one machine's
  `SDL_X11`/`SDL_WAYLAND` choices into another's configure — SDL then aborts
  with "SDL could not find X11 or Wayland development libraries" even though the
  headers are installed. The CI cache is keyed per OS/arch and omits the build
  trees; `npm run gui` also checks the X11/Wayland headers up front on
  Debian/Ubuntu and reports the missing development packages by name (set
  `xbintsc_SKIP_HEADER_CHECK=1` when they live under a prefix that check does
  not know).

## Running the example

```sh
npm run runtime      # rebuild core.a after a runtime change
npm run gui          # build runtime/lib/<os>-<arch>/gui.a (fetches SDL3 once)
npm run media        # optional: build media.a (fetches and builds FFmpeg once)
xbintsc run examples/gui/hello.ts --ext gui
```

Two examples live here: `hello.ts` (HTML/CSS + a reactive counter) and
[`pelican-bike/`](../../examples/gui/pelican-bike), a playable 2D game that uses
positioned layers, CSS sprites, `requestAnimationFrame` and keyboard input — see
its README for controls and the geometry report it prints.

`media.ts` (with `media.html`) demonstrates `<video>`/`<audio>`: it overlays a
CSS progress bar on the video and drives playback with `play()` / `pause()` /
`currentTime` / `volume`. It needs the separate `media` extension, so build and
enable both. Media `src` paths resolve relative to the process working
directory, so run it from the repository root:
`xbintsc run examples/gui/media.ts --ext gui,media`.

Set `XT_GUI_AUTOCLOSE_MS=<n>` to close all windows after `n` milliseconds,
which the e2e test (`tests/e2e/gui-*.test.ts`) uses to run headlessly.

Those suites are skipped on Windows by default (the archive is built and runs
locally through Vulkan, but CI runners have no GPU driver to run it). With a
`gui.lib` and a working GPU, opt in with `xbintsc_GUI_TESTS=1`:

```bat
set xbintsc_GUI_TESTS=1
npx vitest run tests/e2e/gui-layout.test.ts tests/e2e/gui-example.test.ts
```

On a headless Linux box, install the SDL3 build headers and run under Xvfb with a
software Vulkan driver:

```sh
sudo sh scripts/install-gui-deps.sh   # clang, cmake, X11/Wayland headers, Vulkan, Xvfb
npm run runtime && npm run gui
npm run media                         # optional: <video>/<audio> playback
xvfb-run -a --server-args="-screen 0 1280x720x24" \
  npx tsx src/cli/main.ts run examples/gui/hello.ts --ext gui
```

The media e2e suite (`tests/e2e/gui-media.test.ts`) skips itself unless both
`gui.a` and `media.a` exist; `tests/e2e/gui-media-disabled.test.ts` covers the
GUI-only build and runs with `gui.a` alone.

`scripts/install-gui-deps.sh` holds the package list (`apt-get install -y clang
cmake libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev
libxinerama-dev libxfixes-dev libxkbcommon-dev libwayland-dev wayland-protocols
libdecor-0-dev libasound2-dev libpulse-dev libdbus-1-dev libudev-dev libdrm-dev
libgbm-dev libgl1-mesa-dev libegl1-mesa-dev libvulkan-dev mesa-vulkan-drivers
xvfb`) so that every Linux job which builds `gui.a` installs the same set: the
X11 backend is used by default, Wayland is enabled too but not yet exercised.
Install it before `npm run gui` anywhere the archive is built — SDL3 cannot
find a window backend without those headers.

