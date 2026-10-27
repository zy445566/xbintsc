# Packaging and build

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

- Sources live in `runtime/ext_gui/` (C/C++) plus vendored libs under `vendor/`
  (gitignored; fetched on demand). Media decoding lives in
  `runtime/ext_media/` and is built separately.
- `npm run gui` (`scripts/build-gui.ts`) fetches a pinned SDL3 (`SDL3_TAG`,
  default `release-3.2.10`), builds a static SDL3, fetches and builds static
  FreeType (`FREETYPE_VERSION`) and HarfBuzz (`HARFBUZZ_VERSION`), and fetches
  and builds static **glslang** (`GLSLANG_VERSION`, default `16.6.0`) against
  the pinned SPIR-V headers (`SPIRV_HEADERS_VERSION`), and **SPIRV-Cross**
  (`SPIRV_CROSS_VERSION`, default `vulkan-sdk-1.4.313.0`). glslang is the
  runtime GLSL ES compiler behind WebGL and SPIRV-Cross turns its SPIR-V into
  MSL for the Metal backend (see [WebGL](./webgl.md)); both are merged into the
  archive like the other libraries. The script then compiles the engine and
  merges everything into `runtime/lib/<os>-<arch>/gui.a` (or
  `gui.lib` with the MSVC ABI on Windows), same convention as `core.a`,
  statically bundling all three. The merge uses `libtool` on macOS and
  `ar -M` (GNU/LLVM) elsewhere; CMake archives are discovered under the build
  root or `Release/` for either generator style.
- `npm run media` (`scripts/build-media.ts`) builds the optional **FFmpeg**
  backend. It fetches FFmpeg (`FFMPEG_VERSION`, default `7.1`), configures a
  minimal static build with only the demuxers/parsers/decoders the media
  elements need, compiles `runtime/ext_media/media.cpp` against it and merges
  everything into `runtime/lib/<os>-<arch>/media.a` (`media.lib` with the MSVC
  ABI on Windows). `gui.a` does **not** link FFmpeg: a program enables playback
  explicitly with `--ext media`.
- `src/extensions/gui/index.ts` exposes the engine archive through
  `nativeObjects()` (via `findRuntimeLibrary`, so `.a`/`.lib` both work) and the
  OS frameworks through `linkerFlags()`. `src/extensions/media/index.ts` does
  the same for `media.a`/`media.lib` and compiles the registration bridge
  (`runtime/ext_media/media_bridge.c`) as a normal runtime source.
- `scripts/build-gui.ts` compiles with the *same* clang the driver resolves
  (`src/driver/toolchain-provider.ts`); on Windows that is the MSVC-targeting
  LLVM bundled with Visual Studio, and its sibling `clang++`/`llvm-ar` are used
  for CMake, the engine and the archive merge. There are no `gcc`/`g++`
  fallbacks, and Windows always uses `llvm-ar`, so the archive cannot mix object
  formats or C++ runtimes. The Visual Studio toolchain is found by absolute
  path, so no developer prompt is required. On Windows SDL_GPU selects the
  Vulkan backend; no DXIL/`dxc` toolchain is needed.
- `scripts/build-media.ts` uses the same resolved toolchain. On Windows it
  drives FFmpeg's own MSVC mode: `configure --toolchain=msvc --cc=clang-cl`
  compiles static FFmpeg against the static MSVC CRT, `clang++` compiles the
  player, and `llvm-ar` merges the COFF archives into `media.lib`. The compiler
  chain is entirely Visual Studio (clang-cl, `cl.exe`, `lib.exe`, `llvm-ar`).
  FFmpeg's configure is a POSIX shell script and its
  Makefiles need GNU make, so those two build drivers remain: the shell is Git
  for Windows' bundled bash (Git is already required to clone SDL3), and make is
  a native GNU make such as Chocolatey's `make` (or `xbintsc_MAKE`). The script
  rewrites the generated source path to a drive-letter form and skips dependency
  extraction, which native make cannot pass through the MSVC `/showIncludes` awk
  pipeline. The MSVC tools are only needed by configure's toolchain probe; they
  are resolved from the Visual Studio installation, not from a developer prompt.
  `xbintsc_SHELL` and `xbintsc_MAKE` override the discovered shell/make. Headers
  and libraries are copied into `vendor/ffmpeg-build-msvc/dist` by the script
  rather than through `make install`.
- CI builds `gui.a`/`gui.lib` and `media.a`/`media.lib` before assembling the
  release archive so they ship inside `runtime/lib/<slug>/` (the release tarball
  copies the whole `runtime/` tree). The same folders are packed into the
  optional `@xbintsc/<slug>` npm package, so `--ext gui`/`--ext media` work from
  an npm install on the hosts the release builds for. The archives are built and
  the examples are run on Linux (under Xvfb, with the lavapipe software Vulkan
  driver) and macOS;
  the Windows x64 job builds both archives and links a `gui,media` program, so
  the MSVC build is validated even though *running* the examples there stays
  provisional (the hosted runner has no GPU/Vulkan driver). Windows arm64 still
  skips both archives: FFmpeg's MSVC arm64 build is not validated yet.
- The Windows CI steps call `.github/actions/vs-toolchain` →
  `scripts/setup-vs-toolchain.ps1`, which imports the same Visual Studio
  `vcvarsall` environment and bundled LLVM a local build uses (no separate
  Chocolatey LLVM). For the media build the action also installs GNU make
  through Chocolatey and points `xbintsc_MAKE` at it; the FFmpeg source and the
  installed `vendor/ffmpeg-build-msvc/dist` prefix are cached by script hash.
  The configure/make tree itself is not cached (absolute paths and compiler
  options from another machine).
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

On Windows the same commands produce `gui.lib` and `media.lib` from a normal
PowerShell — no developer command prompt. The Visual Studio installation
supplies the MSVC-targeting `clang++`, `clang-cl`, `cl.exe` and `lib.exe`, and
`npm run media` additionally needs a POSIX shell and GNU make for FFmpeg's
`configure`: Git for Windows supplies the shell (and is already needed to clone
SDL3), while GNU make is a native binary — install it with `choco install make`,
or point `xbintsc_MAKE` at one. The script reports whichever tool is missing and
accepts `xbintsc_SHELL` / `xbintsc_MAKE` overrides.

To select that toolchain explicitly (useful in a fresh shell or to pin the same
compiler CI uses), run:

```powershell
pwsh -File scripts/setup-vs-toolchain.ps1
```

It imports `vcvarsall` (MSVC, Windows SDK, MSBuild) and points `xbintsc_CLANG`
at the LLVM bundled with that Visual Studio installation. GitHub Actions uses
the same script through `.github/actions/vs-toolchain`, so no separate
Chocolatey/standalone LLVM is installed there and CI, local builds and the
prebuilt archives share one compiler chain.

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
`gui.lib` and a working GPU, opt in with `xbintsc_GUI_TESTS=1`; if you also ran
`npm run media`, the media suite then exercises the real decoder on Windows too:

```bat
set xbintsc_GUI_TESTS=1
npx vitest run tests/e2e/gui-layout.test.ts tests/e2e/gui-example.test.ts
npx vitest run tests/e2e/gui-media.test.ts
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
`gui.a`/`gui.lib` and `media.a`/`media.lib` exist; `tests/e2e/gui-media-disabled.test.ts`
covers the GUI-only build and runs with the GUI archive alone.

`scripts/install-gui-deps.sh` holds the package list (`apt-get install -y clang
cmake libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev
libxinerama-dev libxfixes-dev libxkbcommon-dev libwayland-dev wayland-protocols
libdecor-0-dev libasound2-dev libpulse-dev libdbus-1-dev libudev-dev libdrm-dev
libgbm-dev libgl1-mesa-dev libegl1-mesa-dev libvulkan-dev mesa-vulkan-drivers
xvfb`) so that every Linux job which builds `gui.a` installs the same set: the
X11 backend is used by default, Wayland is enabled too but not yet exercised.
Install it before `npm run gui` anywhere the archive is built — SDL3 cannot
find a window backend without those headers.

