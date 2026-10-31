# Packaging and build

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

- Sources live in `runtime/ext_gui/` (C/C++) plus vendored libs under `vendor/`
  (gitignored; fetched on demand). Media decoding lives in
  `runtime/ext_media/` and is built separately.
- `npm run gui` (`scripts/build-gui.ts`) fetches a pinned SDL3 (`SDL3_TAG`,
  default `release-3.2.10`), builds a static SDL3, fetches and builds static
  **FreeType** (`FREETYPE_VERSION`, default `2.13.3`) and **HarfBuzz**
  (`HARFBUZZ_VERSION`, default `10.1.0`) for the optional `shaping` feature,
  and fetches and builds static **glslang** (`GLSLANG_VERSION`, default
  `16.6.0`) against the pinned SPIR-V headers (`SPIRV_HEADERS_VERSION`), and —
  **on macOS only** — **SPIRV-Cross** (`SPIRV_CROSS_VERSION`, default
  `vulkan-sdk-1.4.313.0`). glslang is the runtime GLSL ES compiler behind WebGL
  and SPIRV-Cross turns its SPIR-V into MSL for the Metal backend (see
  [WebGL](./webgl.md)); both are merged into the archive like the other
  libraries. Metal is the only SDL_GPU backend that consumes MSL, so on
  Windows/Linux the script neither fetches nor merges SPIRV-Cross:
  `webgl_msl.cpp` compiles to nothing there and the ~2.5 MiB it contributes
  would be unreachable. The default text path uses the bundled
  **stb_truetype** (`runtime/ext_gui/stb_truetype.h`); HarfBuzz + FreeType are
  only extracted when the `shaping` feature is active.
  Every library (SDL3, FreeType, HarfBuzz, glslang, SPIRV-Cross and the engine)
  is compiled with `-ffunction-sections -fdata-sections`, and SDL is configured
  without the subsystems the engine never calls (`SDL_JOYSTICK`, `SDL_HAPTIC`,
  `SDL_SENSOR`, `SDL_CAMERA`, `SDL_RENDER`, `SDL_OPENGL`/`SDL_OPENGLES`,
  `SDL_OFFSCREEN`, `SDL_HIDAPI`). HarfBuzz omits the APIs the shaping backend
  never uses (`HB_NO_DRAW`/`PAINT`/`COLOR`/`NAME`/`STYLE`/`METRICS`/…, while
  keeping `CFF`/`OPEN`/`VAR` for OTF and variable fonts) and is built with
  `HB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR`, because `src/hb.hh` raises its warning
  set to errors and two lean defines leave macOS/CoreText helpers unreferenced
  (`HB_NO_MMAP` and `HB_NO_OT_FONT_GLYPH_NAMES`). The linker then drops
  the unreachable functions (`--gc-sections`, `-dead_strip`, or link.exe's
  default `/OPT:REF`). The
  script then compiles the engine and
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
  ABI on Windows). `gui.a` does **not** link FFmpeg: the compiler selects the
  `media` link feature for a program that may play media and only then links
  `media.a` (see [Link features](#link-features)).
- `src/extensions/gui/index.ts` exposes the engine archive through
  `nativeObjects()` (via `findRuntimeLibrary`, so `.a`/`.lib` both work) and the
  OS frameworks through `linkerFlags()`. Its `linkFeatures()` (in
  `src/extensions/gui/features.ts`) declares `media`, `gpu`, `images`, `shaping` and `webgl`; the driver
  compiles their bridges only when a feature is active.
- glslang (and SPIRV-Cross on macOS) is merged into `gui.a` like SDL3, but the
  engine reaches it only through the `gpu` feature's dispatch table. A program
  that never compiles GLSL does not extract those archive members.
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
  optional `@xbintsc/<slug>` npm package, so `--ext gui` (with automatic
  `media`/`gpu`/`images`/`shaping`/`webgl` selection) works from
  an npm install on the hosts the release builds for. The archives are built and
  the examples are run on Linux (under Xvfb, with the lavapipe software Vulkan
  driver) and macOS;
  the Windows x64 job builds both archives and links a `gui` program, so
  the MSVC build is validated even though *running* the examples there stays
  provisional (the hosted runner has no GPU/Vulkan driver). The macOS run is
  provisional too: the Intel runner's virtualised GPU lacks
  `MTLGPUFamilyMac2`, so the engine exits **77** ("cannot run a GUI here") and
  the job records a skip instead of compiling a suite that cannot present.
  Windows arm64 still skips both archives: FFmpeg's MSVC arm64 build is not
  validated yet.
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
- CMake build trees live *next to* their sources (`vendor/sdl-build-lean`,
  `vendor/freetype-build-lean`, `vendor/harfbuzz-build-lean`,
  `vendor/glslang-build-lean`, `vendor/spirv-cross-build-lean`), never inside them: a
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

## Link features

`gui` is the only user-facing extension, but two heavy halves are **optional link
features** selected per program:

| Feature | What it links | Selected when the program may use it |
| --- | --- | --- |
| `media` | `media_bridge.c` + `media.a` (FFmpeg) | an HTML `<video>`/`<audio>` tag, `createElement("video"/"audio")`, `new Audio()`, `decodeAudioData`, or runtime-built markup |
| `gpu` | `gpu_bridge.c` (the implementation is already in `gui.a`) | a `getContext("webgl"/"webgl2"/"experimental-webgl")` (or a kind decided at run time), any `webgl` string, `mix-blend-mode`/`mixBlendMode`, or runtime-built markup |
| `images` | `image_bridge.c` (the implementation is already in `gui.a`) | `<img>`, inline `<svg>`, `background-image`/`url(...)`, a `.svg` path, `drawImage`/`createImageBitmap`/`Image`, or runtime-built markup |
| `shaping` | `text_bridge.c` (the implementation is already in `gui.a`) | non-ASCII text (CJK/Cyrillic/Greek/…), an escaped/entity codepoint, an author `@font-face` rule (custom fonts may be CFF/OTF), or runtime-built markup |
| `webgl` | `webgl_bridge.c` (the implementation is already in `gui.a`; requires `gpu`) | a WebGL `getContext(...)`, a `webgl` string, or runtime-built markup |

The driver scans the bundled AST (`src/driver/usage.ts`): every string literal
(including imported `.html` and the CSS inlined from it), identifiers and
property names, each `getContext(...)` kind, and whether HTML/CSS can be built
from data the compiler cannot see. Simple `const` aliases are resolved;
anything unresolved makes the program “dynamic markup” and enables the optional
features conservatively.

When a feature is active, the driver compiles its C **bridge only for that
build** and links it before `gui.a`:

- `media` → `runtime/ext_media/media_bridge.c`; its constructor installs the
  decoder table and references `xt_media_backend_*`, which is what extracts the
  FFmpeg members from `media.a`.
- `gpu` → `runtime/ext_gui/gpu_bridge.c`; its constructor calls
  `xt_gpu_feature_register` in `gpu_feature.cpp`, which installs the
  `XtGpuShaderApi` table and references `compile_glsl` / `cross_compile_msl` /
  the reflection helpers, extracting glslang (and SPIRV-Cross on macOS).
- `images` → `runtime/ext_gui/image_bridge.c`; its constructor installs
  `XtImageApi` via `image_feature.cpp`, which references stb_image and NanoSVG.
  Layout, paint, the canvas `drawImage` path and the app-icon path call through
  that table, so a text-only page links neither decoder.
- `shaping` → `runtime/ext_gui/text_bridge.c`; its constructor installs
  `TextApi` via `text_hb.cpp`, which references HarfBuzz and FreeType. Without
  it the engine uses the built-in stb_truetype backend, so a Latin-only page
  links neither library.
- `webgl` → `runtime/ext_gui/webgl_bridge.c`; its constructor installs
  `XtWebglApi` via `webgl_feature.cpp`, which references the WebGL context and
  DOM bindings. The core reaches `getContext`, the canvas width/height setters,
  `createImageBitmap`, the host types and the renderer through that table, so a
  page with no WebGL links none of `webgl_*`/`dom_api_webgl*`.

A bridge is the “strong reference” that makes the linker search the archive; with
no bridge, the engine's table stays null and the implementation members are never
extracted. That is why the large libraries can stay in the existing archives
without a new package layout.

Overrides: `--feature media,gpu,images,shaping,webgl` (or `all`) forces a
feature on; `--no-feature media` keeps it out even when detection finds a use.
`webgl` requires `gpu`, and the resolver activates both together. An app icon
forces `images` because the icon is decoded through the same pipeline. `--ext media`
is a deprecated alias for `--ext gui --feature media`. If an auto-detected
feature's archive is missing, the build warns and continues (the element/context
degrades as documented); a forced feature makes it an error.

> Detection is static by design. A page that builds HTML/CSS from `fetch`,
> `readFileSync` or other runtime data should pass `--feature all` (or the
> specific feature). `--verbose` prints the selected set, e.g.
> `xbintsc: link features: none`.

### Binary size

The lean build is what makes a DOM-only program small. On Windows x64 a
`hello`-sized program drops from roughly 6.4 MiB to about 3.1 MiB once SDL is
configured as above, function/data sections let `/OPT:REF` discard what the page
never reaches, the WebGL/images link features stay off, and text uses the
bundled stb_truetype instead of HarfBuzz + FreeType. The remaining weight is
mostly the static CRT, the lean SDL (~1 MiB), stb_truetype (~0.1 MiB), and the
DOM/CSS/layout/paint engine itself (~1.2 MiB). Linux and macOS use their own
linker sections (`--gc-sections`/`-dead_strip`) and differ somewhat, but the
relative ranking is the same.

WebGL and images/SVG are link features (roughly 0.4 MiB of WebGL context/DOM
code and 0.23 MiB of stb_image/NanoSVG that a DOM-only page does not link).
Canvas2D, Web Audio and the `mix-blend-mode` compositor are not gated yet; a
program that uses none of them still pays for their API surface (roughly
0.1–0.2 MiB each).

The text trade-off is deliberate. The default backend is stb_truetype: only
TrueType outlines and a per-codepoint mapping plus the font's `kern` table, so
CFF/OTF and variable fonts, OpenType ligatures and complex-script shaping
(Arabic/Indic/Thai) are not available. The fallback chain still selects another
face for codepoints the primary lacks. Selecting the optional **`shaping`**
feature (automatically for non-ASCII text or an author `@font-face` rule, or
with `--feature shaping`) links HarfBuzz + FreeType back in (~1.2 MiB) and
restores full OpenType shaping and CFF/OTF/variable-font support. A Latin-only
DOM page therefore stays near 3.1 MiB; a Chinese/complex-script page is closer
to 4.3 MiB.

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
`currentTime` / `volume`. The compiler sees the elements in `media.html` and
links the `media` feature automatically; build `media.a` once with
`npm run media`. Media `src` paths resolve relative to the process working
directory, so run it from the repository root:
`xbintsc run examples/gui/media.ts --ext gui`.

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

