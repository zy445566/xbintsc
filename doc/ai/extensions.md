# Extensions: Node modules, GUI, media, C++/Rust libraries

The compiler core is platform-agnostic. Anything platform-specific — Node's
`fs`, an HTML/CSS renderer, your own C++ library — arrives as an **extension**
that contributes module bindings plus the C/C++ sources or objects to link.

## The rule that trips people up

**An import of an extension-provided module fails until the extension is
enabled.** xbintsc knows the module exists and says exactly what to pass:

```bash
$ xbintsc run app.ts            # app.ts: import { readFileSync } from "fs";
error TS6001: module 'fs' is provided by the 'node' extension; pass --ext node
```

```bash
xbintsc run app.ts --ext node
```

Enable several at once with a comma-separated list: `--ext node,gui`. In a
project, put them in `xbintsc.config.json` so no flag is needed:

```json
{ "entry": "src/app.ts", "outDir": "build", "extensions": ["node"] }
```

## Bundled extensions

### `node` — Node built-in modules

Enable with `--ext node`. A module is importable by its bare name or the
`node:` prefix (`import { readFileSync } from "node:fs"`), and both spellings
resolve to the same implementation.

| Module | Notes |
| --- | --- |
| `fs` | **synchronous API only** (`readFileSync`, `writeFileSync`, …); `fs/promises` is separate |
| `fs/promises` | promise-based file APIs; the blocking I/O runs on the async I/O worker pool and settles on the reactor |
| `path` | also hooks namespace dispatch, so `path.join(...)` works |
| `os` | |
| `process` | `process.cwd()`, `argv`, `env`, …; namespace dispatch too |
| `buffer` | |
| `crypto` | |
| `stream`, `stream/promises` | |
| `events` | |
| `net`, `dgram`, `http` | sockets and servers on the xbintsc event loop (see the async caveat below) |
| `child_process` | |
| `worker_threads` | |
| `util`, `querystring`, `url`, `assert`, `test`, `zlib` | |

Coverage per module (exact functions and options) is in
[../implemented/node.md](../implemented/node.md); what is missing is in
[../unimplemented/node.md](../unimplemented/node.md).

Remember the runtime model before building on `net`/`http`: the event loop runs
**after** the program body, but `async`/`await` suspends on pending promises, so a
promise settled from a socket callback, a timer or an offloaded file/HTTP job can
be awaited. `net.connect` itself is still blocking. See
[language-support.md](./language-support.md).

### `gui` — HTML/CSS window

Enable with `--ext gui` and import the `gui` module:

```ts
import { createWindow, run } from "gui";
```

It is a self-contained GPU-accelerated renderer (own HTML parser, CSS cascade,
layout and compositor) — not a system WebView. The compiler side also inlines
local `<link rel="stylesheet">` files (and their `@import`s) into `<style>`
blocks, because the runtime parser only reads inline `<style>`; and it rewrites
a bundled three.js module's `WebGLRenderer` to use the real WebGL 2 backend:
only its shader unroller is patched, and no Canvas2D fallback is bundled. It
ships as a per-platform prebuilt `gui.a`/`gui.lib` because it is C++. `<img>` decodes through the engine
itself; `<video>`/`<audio>` do **not** work with `gui` alone — they need the
separate [`media`](#media--videoaudio-decoding) extension. `<canvas>` is
supported too: `getContext("2d")` returns a context whose fills, paths,
gradients, text and `drawImage` are rasterised by a self-written CPU rasteriser
(plus line caps/joins, dashed strokes, `ellipse` and `roundRect`), and element
handles expose `canvas` drawing plus `width`/`height`. CSS
`transform`/`transform-origin` work as paint-time effects, so a box (and the
text, images and children in it) can be translated, scaled and rotated without
reflowing anything — animate it per frame from `requestAnimationFrame` or with
`transition: transform` (see `examples/gui/pelican-svg`). The CSS subset also
covers `@media`, `::before`/`::after` generated content, multi-layer linear/radial
backgrounds, `text-shadow`, `clip-path` and `filter` colour/shadow effects;
`backdrop-filter` and true group blur are documented approximations or
unsupported. If the archive is
missing for your platform, the build fails with an actionable message; design
and roadmap live in [../gui.md](../gui.md) (overview; the engine is documented by
function under [`doc/gui/`](../gui/)), and the scripting surface in
[../gui-scripts.md](../gui-scripts.md). See
[../gui/canvas.md](../gui/canvas.md) for the canvas API and its limits.

Pages written for the browser also get the browser globals they reach for —
`window`/`self`, `document`, `navigator`, `location`, `localStorage`,
`requestAnimationFrame`, `matchMedia`, `getComputedStyle`, `alert`/`prompt`,
`escape`, the `Event` family and a working Web Audio implementation
(`AudioContext` with gain, oscillator, buffer-source, biquad, compressor,
stereo-panner, constant-source and delay nodes, a real 48 kHz mixer, per-sample
AudioParam automation, and `decodeAudioData` for WAV in-engine plus compressed
formats through `media`) — plus a frame pump that
starts at `loadHTML` and re-arms only while animation-frame callbacks remain, so
a page that never calls `run()` still paints and then exits. **WebGL 1.0 and 2.0 are implemented**: `canvas.getContext("webgl")` /
`"webgl2"` return a GPU context whose GLSL ES shaders are compiled to SPIR-V by
a bundled glslang — consumed by Vulkan, or cross-compiled to MSL by bundled
SPIRV-Cross on Metal — and drawn through SDL_GPU; the page composites the colour
texture without a CPU readback, and WebGL 2 adds VAOs, instancing, integer
attributes, sampler objects, `texStorage2D` and buffer copies (see
[../gui/webgl.md](../gui/webgl.md)). WebXR, `<video>`/`<audio>` without
`media`, and streaming are **not** implemented:
`typeof` answers `"undefined"` so a page takes its own fallback branch. `fetch`
is **not** a GUI extension feature: the core runtime provides it (`http://`,
plus local `file://` and relative paths; no HTTPS or streaming), so GUI
programs can use it too. See [../implemented/fetch.md](../implemented/fetch.md).

### `media` — video/audio decoding

Enable with `--ext media` **together with** `gui` (`--ext gui,media`; the
registry rejects `media` on its own). It links a separate FFmpeg-backed
`media.a`/`media.lib`, so adding `<video>`/`<audio>` playback does not make a
GUI-only binary any larger.

The DOM API is the same as for any HTML media element: `play()` / `pause()` /
`load()`, the `paused` / `ended` / `duration` / `currentTime` / `volume` /
`muted` / `loop` / `autoplay` / `videoWidth` / `videoHeight` / `readyState` /
`src` properties, and `loadedmetadata`/`play`/`pause`/`ended`/`timeupdate`/
`error` events. There are no built-in controls — draw your own chrome with
elements and CSS (see `examples/gui/media.ts`). Video frames and audio PCM are
decoded on the main thread; video goes through the image texture path. Without
the extension, opening a media source fails and fires `error` (the element and
its API still exist). The archive is built for every host: POSIX produces
`media.a`, while Windows produces `media.lib` from FFmpeg compiled with the
MSVC-targeting `clang-cl`, so it links with `gui.lib`. Building it on Windows
needs a POSIX shell (Git for Windows' bundled bash) and a native GNU make
(Chocolatey `make`, or `xbintsc_MAKE`). See
[../gui/build.md](../gui/build.md). Without the archive, or on a host where
`npm run media` has not run, media elements report `error`; see
[../gui/known-issues.md](../gui/known-issues.md).

## Third-party npm packages

Bare specifiers that no enabled extension claims are looked up in `node_modules`
and bundled **as source**, including CommonJS packages (the bundler gives them a
synthetic `module`/`exports` and lowers their `require` calls). What does not
work: dynamic `require(<expression>)`, packages that depend on Node globals such
as `__dirname`, and circular dependencies. `require()` in your own source is
rejected with a hint to convert to `import`.

Practical consequence: for a Node program, enabling `--ext node` is usually the
right answer; reach for an npm dependency only when you must.

## Native extensions (C++ / Rust, no compiler changes)

Any code that exposes `extern "C"` entry points with the runtime ABI can be
linked in:

```c
xt_value my_fn(int32_t argc, xt_value *argv);
```

Build it with an **external** toolchain (clang++ or cargo), describe the
artifacts in a JSON manifest, and pass the manifest:

```bash
xbintsc build demo.ts --ext-native ./xbintsc.manifest.json
xbintsc build demo.ts --ext-native a.json,b.json      # several
```

```jsonc
{
  "name": "mathx-cpp",                       // required, unique
  "objects": ["build/mathx.o"],              // .o / .a / .lib, relative to this file
  "linkerFlagsByPlatform": {                 // C++/Rust runtimes
    "linux":  ["-lstdc++", "-lm"],
    "darwin": ["-lc++"],
    "win32":  ["-lmsvcprt"]
  },
  "builtins": { "cppClamp": { "symbol": "mathx_clamp" } },   // no import needed
  "modules": {
    "mathx": { "exports": { "add": { "symbol": "mathx_add" } } }
  }
}
```

`import { add } from "mathx"` then lowers to the native symbol exactly like a C
runtime binding; `builtins` are globally callable without importing.

Authoring helpers: `runtime/xt_ext.h` (C/C++) and `runtime/xt_ext.rs` (Rust) —
they wrap the argument and value helpers. Key ABI facts: values are 64-bit
NaN-boxed words; strings are **not** NUL-terminated, so pair `xt_string_data`
with `xt_string_length_value`; the runtime never moves or frees memory it handed
out. Build with the same clang/ABI that xbintsc resolves (`xbintsc doctor`
prints it). On Windows that must be an MSVC-targeting clang — normally the LLVM
bundled with Visual Studio; a developer prompt is not required.

Working projects to copy: [`examples/extensions/cpp`](../../examples/extensions/cpp)
and [`examples/extensions/rust`](../../examples/extensions/rust); the full guide
is [../../examples/extensions/README.md](../../examples/extensions/README.md).

## Programmatic registration

```ts
import { build, createDefaultRegistry, nativeExtensionFromManifest, nodeExtension } from "xbintsc";

const extensions = createDefaultRegistry()
  .register(nodeExtension)
  .register(nativeExtensionFromManifest("./xbintsc.manifest.json"));

build("demo.ts", { extensions });
```

The registry is also how a host tool hints at a disabled extension, which is what
produces the "pass `--ext node`" message instead of a confusing downstream error
([../../src/extensions/catalog.ts](../../src/extensions/catalog.ts)). The list of
bundled extensions is `bundledExtensions()`.
