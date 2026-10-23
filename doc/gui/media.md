# Media: images, video and audio

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

## Images (M6a)

`runtime/ext_gui/image.{h,cpp}` wraps the vendored **stb_image** header
decoding PNG/JPEG/BMP/GIF/TGA to RGBA8, with a decode cache and a header-only
size cache (`stbi_info`). Paths accept a `file://` prefix and percent-encoding.
`runtime/ext_gui/svg.{h,cpp}` adds SVG on top: it wraps the vendored **NanoSVG**
library (parse + anti-aliased scanline rasteriser), detects an SVG by extension
or by its contents, and produces the same RGBA8 `Image` the pipeline already
consumes. `stb_image` cannot decode SVG, so this is what makes
`<img src="logo.svg">` work. The same parser is used for an inline `<svg>`
subtree, whose DOM nodes are serialized into a document first
(`inline_svg.{h,cpp}`).

- `<img>` is a replaced element (`display: inline-block`): layout gives it the
  CSS size when set, otherwise the intrinsic pixel size, and preserves the
  aspect ratio when only one axis is constrained. The intrinsic size is read
  from the file header (no full decode) while building the box tree, and for an
  SVG it comes from the parsed `width`/`height` or the `viewBox` (falling back
  to 300x150 when neither is present).
- Paint emits one `PaintImage` per `<img>`; the renderer uploads each unique
  `src` to an RGBA texture and draws textured quads, batching consecutive quads
  that share a texture. A **bitmap** decodes at its intrinsic resolution and is
  cached by path; a **vector** (SVG) is rasterised for the drawn size in device
  pixels (logical size × the window's pixel density), preserving the viewBox
  aspect ratio, and cached by that size, so scaling or a HiDPI display produces
  a crisp render instead of a magnified bitmap.
- An **inline** `<svg>` element is a replaced element like `<img>`, but its
  source is the DOM subtree itself. `runtime/ext_gui/inline_svg.{h,cpp}`
  serializes that subtree back into a standalone SVG document, restoring the
  case-sensitive spellings NanoSVG matches (`viewBox`, `preserveAspectRatio`,
  `linearGradient`, `gradientUnits`, …) that the HTML parser lowercased. Layout
  sizes the box from the `width`/`height` attributes, else the `viewBox`, else
  300x150; paint carries the markup on its image op and the renderer parses and
  rasterises it at the drawn device pixel size through the same NanoSVG cache as
  an SVG file. A DOM mutation re-serializes the subtree on the next layout (so
  `setAttribute` and `innerHTML` both repaint the artwork), and the markup cache
  is bounded (least-recently-used) so an animated document cannot grow it
  without limit.
- The parse is cached per path; the parse and rasteriser state are released in
  `xt_svg_shutdown` (`xt_image_shutdown`).

Document CSS styles the `<svg>` **box** — size, margins, background, borders,
`transform`, opacity, `display` — exactly as it styles any other element. The
artwork inside it keeps SVG semantics: presentation attributes and the
`class`-based rules of an SVG `<style>` block are read by NanoSVG, not by the
HTML cascade, so `fill`/`stroke` are not CSS properties here. For layout, paint,
hit testing and events the whole `<svg>` is one replaced box: its children get no
DOM boxes, so a click's `e.target` is the `<svg>` element, not a `<rect>`. The
renderer caches one texture per markup-and-size pair, so a program that rewrites
its artwork every frame keeps a texture per variant; prefer animating the box
(`transform`, `opacity`) or swapping between a small set of documents.

Still to do for images: CSS `background-image: url(...)`, `data:` URIs,
`object-fit` and 9-slice borders.

Both kinds of SVG share NanoSVG's subset: `path`, `rect`, `circle`, `ellipse`,
`line`, `polyline`, `polygon`, groups (`g`), `defs` with linear/radial gradients
and class-based `<style>` rules rasterise; `<text>`, `<use>`, filters, masks and
clip paths are not rendered (see [Known issues](./known-issues.md)).

## Video & audio (M13)

Media playback is split across two extensions so a GUI program does not pay for
a decoder it never uses:

- **`gui`** (`gui.a`) owns the `<video>`/`<audio>` elements, the DOM API, the
  renderer integration and the SDL audio sink, but contains **no FFmpeg**.
  `runtime/ext_gui/media.{h,cpp}` declares the player interface and dispatches
  through a table the media backend installs at startup
  (`runtime/ext_gui/media_api.h`).
- **`media`** (`media.a`, `runtime/ext_media/`) is the optional decoder
  backend: `runtime/ext_media/media.cpp` wraps a statically vendored **FFmpeg**
  build, and `runtime/ext_media/media_bridge.c` registers it with the engine.
  Enable it with `--ext media` (or `--ext gui,media`). `media` requires `gui`;
  the registry rejects the combination in the wrong order.

Without the `media` extension the element still exists and its DOM API is
callable, but opening a source fails and fires `error`. That is also how Windows
behaves today: the archive is POSIX-only. There is no WebView and no platform
media framework in the path either way.

- **Decoding.** `avformat_open_input` opens the `src` (a path — `file://` is
  accepted, but media paths are *not* percent-decoded, unlike images), the
  best stream is selected for each kind, and a software decoder yields
  `yuv/rgb` frames which `sws_scale` converts to RGBA8 at the coded size. The
  supported containers/codecs are the minimal FFmpeg configuration from
  `scripts/build-media.ts`: MP4/MOV/MKV/AVI/FLV/MPEG-TS/M4V/ASF, MP3/OGG/WAV/FLAC/
  AAC; H.264, HEVC, VP8, VP9, AV1, MPEG-4, MJPEG video; AAC, MP3, Opus, Vorbis,
  FLAC, ALAC, AC3/E-AC3, WMAv2 and the raw PCM formats audio.
- **Video.** `<video>` is a replaced element like `<img>` (UA default
  `300x150`, overridden by the decoded size via `setMediaSize`). Paint emits the
  same `PaintImage` as an image; the renderer uploads each new decoded frame to
  an RGBA texture (`ensureMediaTexture`) and draws a textured quad, so the GPU
  scales it to the CSS box instead of the CPU resizing. Playback timing is the
  process monotonic clock, not the frame delta.
- **Audio.** There is no audio callback: the decoder is pushed on the main
  thread as it is ticked and writes into an `xtgui::AudioSink`. The GUI
  implements that sink in `gui_media.cpp` with `SDL_OpenAudioDeviceStream` in
  push mode (`NULL` callback), buffering about 0.25 s ahead at 48 kHz stereo
  `SDL_AUDIO_F32`. `volume` maps to `SDL_SetAudioStreamGain`; audio is
  best-effort and is skipped when no device is available (headless CI). Keeping
  the sink in `gui.a` means `media.a` depends only on FFmpeg.
- **Threading.** The engine is single-threaded and not thread-aware, so all
  decoding happens on the main thread inside `xt_gui_media_tick` — no decode
  thread, no cross-thread GC exposure.
- **DOM API.** Element handles expose the media surface directly (see
  `runtime/ext_gui/dom_api_media.cpp` and the prototype in
  `dom_api_proto.cpp`): `play()` / `pause()` / `load()`, and the
  `paused` / `ended` / `duration` / `currentTime` / `volume` / `muted` / `loop`
  / `autoplay` / `videoWidth` / `videoHeight` / `readyState` / `src`
  properties. Media elements also emit `loadedmetadata`, `play`, `pause`,
  `ended`, `timeupdate` (every 250 ms) and `error`, so the usual
  `addEventListener` pattern works.
- **No built-in controls.** `<video controls>` is deliberately *not*
  implemented: the element always renders the raw frame, and the program draws
  its own chrome (a progress bar, buttons) with ordinary elements and CSS. See
  `examples/gui/media.html` (`examples/gui/media.ts`) for the pattern.
- **Lifecycle.** Players are owned per window (`XtGuiMediaEntry`, keyed by
  node). `xt_gui_media_sync` drops entries whose node is gone, reopens on a
  `src` change, and opens on first use; `xt_gui_media_shutdown` runs on window
  close/quit/`loadHTML`.

Still to do for media: `<video controls>`, separate audio/video clock drift
correction, hardware decode, streaming protocols (only `file` is enabled),
subtitles, `playbackRate` and per-frame `requestVideoFrameCallback`.

