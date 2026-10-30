# Paint, GPU and text

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

## Paint and display list (M4a/M4b)

`runtime/ext_gui/paint.{h,cpp}` walks the layout tree in painter's order and
emits a backend-agnostic `DisplayList`: a painter-order `ops` list of
**rectangles** (backgrounds and four solid border edges, with `border-radius`),
**text runs** (each carrying its text, colour and resolved `FontSpec`) and
**images**, backed by parallel payload arrays. Each op also carries the active
clip rectangle (the padding box of the nearest `overflow: hidden` ancestor) and
up to two indices into the display list's triangulated `clip-path` polygons, so
`overflow`/`clip-path` clipping and `z-index` ordering survive into the renderer.
`DisplayList::dump()` feeds `paintList()`/`paintCount()`.

`opacity` multiplies the alpha of every colour painted by the box and its
descendants (the values multiply down the paint chain), so fractional opacity and
the `opacity` transitions the CSS parser accepts are visible. It is a
per-shape approximation, not an offscreen group: overlapping children of a
translucent box composite individually, whereas a browser composes the group
once and then applies the alpha. `opacity: 0` still skips the whole subtree.
Hiding and revealing a layer with `opacity: 0` on the hidden state is what
`examples/gui/pelican-bike`'s game-over overlay does.

`runtime/ext_gui/renderer.{h,cpp}` turns that list into batched vertex buffers
per window (shapes, glyph quads and images) and merges consecutive ops that
share a pipeline, clip and texture into **draw runs**. Each run is drawn with
its own GPU scissor rectangle (scaled to device pixels), so painter order and
`overflow` clipping are applied per op through the SDL_GPU graphics pipelines:

- The shared pipelines are created lazily from the device's supported shader
  format. Each of SDL_GPU's three backends consumes a different binary and none
  of them compiles GLSL/HLSL at runtime, so the engine ships all three and
  `selectShader` (`renderer_shaders.h`) picks the one the device accepts:
  **MSL** for Metal (compiled by SDL from the embedded source),
  **SPIR-V** for Vulkan (`runtime/ext_gui/spirv/*.{vert,frag}`, compiled by
  `glslc`), and **DXIL** for Direct3D 12 (`shaders.hlsl`, compiled by `dxc`).
  `scripts/build-gui-shaders.mjs` regenerates the embedded blobs and the result
  is committed, so nothing extra is needed to build the engine.
  The descriptor bindings differ per format — SPIR-V follows what
  `SDL_gpu_vulkan.c` builds (uniforms `set=1, binding=0`, samplers
  `set=2, binding=0`, textures `set=2, binding=1`) — and glslc names every entry
  point `main` where MSL/DXIL keep the descriptive names.
  Note that SDL 3.2.10's Direct3D 12 backend cannot create a graphics pipeline
  whose shaders declare a uniform buffer (it fails with `E_INVALIDARG`); the
  engine passes the viewport that way, so the DXIL path is built but not usable
  until that is resolved upstream. Windows does not hit this: SDL_GPU selects the
  Vulkan backend there (confirmed by `win.driver()`), which consumes the SPIR-V
  blobs.
- A **rounded-rectangle distance field** in the shape fragment shader gives
  antialiased fills; the vertex carries `position`, `local`, `half extents`,
  `radius` and colour, and a viewport-size push constant does the projection.
  Alpha blending is enabled.
- The text pipeline samples a **single shared grayscale glyph atlas**
  (`R8_UNORM`, 2048², shelf-packed, LINEAR filtering) and draws each glyph as a
  textured quad (`position`, `uv`, colour), modulating alpha by the coverage.
- Geometry is uploaded only when the document or viewport changes
  (`geometry.dirty`), so steady-state frames are bind-and-draw.

Each op also carries the `Matrix2D` of the box it came from
(`runtime/ext_gui/transform.h`, `PaintRect::transform` and friends). It is
applied while the vertex buffer is built, not in the shader: `pushRect` maps its
four corners through the matrix (the distance-field `local`/half-extent/radius
fields stay in the *box* frame, so a rounded corner stays round after a
rotation), and `pushQuad`/`pushGlyph` map the glyph or image quad the same way.
That kept the MSL/SPIR-V/DXIL shaders and the pipelines untouched — a transform
costs a few multiplies per vertex and nothing per pixel. A clip rectangle of a
transformed box is the bounding box of the transformed padding box, because the
GPU scissor is axis aligned. Text is the one case that needs more than
multiplying corners: a scaling transform would magnify an atlas raster, so the
atlas is asked for glyphs at the device scale **times the matrix's
magnification** (clamped to 0.5–4x, since the atlas is finite) and the quad is
divided back into logical units, exactly the way HiDPI scaling already worked.

The window background (`setBackground`) is the render-pass clear colour.

### Backgrounds, generated content and effects (M19)

`background-color` and every `background-image` layer are painted. A
comma-separated layer list is painted bottom-up (the first declaration layer is
topmost), and both gradient kinds are supported:

- a **linear** gradient is sampled at 18 points (64 when repeating) and emitted
  as solid rectangles; `to <side>`, `deg`/`grad`/`rad`/`turn`, percentage or
  length (`px`/`em`/…) offsets, the `color <start> <end>` shorthand and
  multi-stop colours work (the band walk is axis-aligned, so an oblique angle
  uses the nearest axis);
- a **radial** gradient is resolved per pixel in `renderer_gradient.cpp` into one
  RGBA texture that is drawn as a single image quad (`PaintGradient`). Nesting the
  bands instead would composite their alpha once per band — since every band
  covers the whole interior — so a vignette whose stops fade to `transparent`
  painted an opaque disc over the scene. Shape/size keywords
  (`circle`/`ellipse`, `closest-side`/`farthest-side`/`closest-corner`/
  `farthest-corner`, explicit radii) and `at <position>` are parsed; a
  keyword-sized ellipse keeps the box's aspect ratio and is scaled until it meets
  the side or corner, as CSS does. Stops interpolate in premultiplied alpha, and
  the last stop extends past the ending shape over the rest of the box.

`repeating-linear-gradient()` and `repeating-radial-gradient()` tile the stop
pattern from 0 to the last stop's offset across the whole box; the stop list is
not forced to end at 100%, so `red 0%, blue 10%` repeats every 10%. A normal
gradient whose last stop is below 100% holds that colour for the rest of the box,
as in CSS.

The background colour sits under every layer. Gradients are partially
transparent if their stops are; a radial gradient is exact per pixel, while a
linear one still samples 18 bands and can show banding on a large soft ramp.

**Generated content** needs no new display-list kind: `::before`/`::after` are
ordinary layout boxes owned by the per-restyle arena in `style.cpp`, so their
background, border, text and transforms flow through the same paint walk.
`content` is resolved from strings, `attr()` and open/close quotes.

**`text-shadow`** is emitted as shadow `PaintText` runs *before* the real run, so
the text pipeline draws them without a new shader. A blurred shadow is nine
translucent copies spread around the offset (the same approximation `filter:
blur()` uses).

**`clip-path`** is resolved in `paint_clip.cpp` to a device-space polygon (the
box transform is applied to every point), triangulated with ear clipping, and
stored on `DisplayList::clip_paths`. Ops carry up to two path indices on their
`PaintClip`; `renderer_clip.cpp` intersects each convex shape/glyph/image quad
with those triangles at upload time. The result is exact even for concave
`polygon()` shapes, needs no stencil or shader change, and the edges are hard
(no analytic antialiasing). Intersecting two nested paths is supported; deeper
nesting keeps the outermost two.

**`filter`** colour functions (`brightness`, `contrast`, `grayscale`, `sepia`,
`invert`, `saturate`, `hue-rotate`, `opacity`) are folded into each painted
colour, so a box and its descendants see the filter. `blur()` and
`drop-shadow()` are approximations at the display-list level: blur spreads nine
translucent copies per shape, drop-shadow is a box/text silhouette. This keeps
the ordinary painter single-pass at the cost of group semantics: overlapping
descendants of a blurred/filtered group can darken, and `backdrop-filter` cannot
sample what is behind the element, so it parses and cascades but paints nothing.

**`mix-blend-mode`** is the exception that does need an offscreen layer. A box
with a non-`normal` mode is wrapped in `LayerBegin`/`LayerEnd` markers (visible
in `paintList()` as `layer blend=…` / `end-layer`); the renderer draws that group
to a full-viewport offscreen texture and composites it over the scene with a
small GLSL blend shader. All sixteen CSS modes (`multiply`, `screen`, `overlay`,
`darken`, `lighten`, `color-dodge`, `color-burn`, `hard-light`, `soft-light`,
`difference`, `exclusion`, `hue`, `saturation`, `color`, `luminosity`) are
implemented with the compositing-spec formulas. The shader is compiled at run
time by the bundled glslang (SPIR-V for Vulkan) and cross-compiled to MSL by
SPIRV-Cross (Metal); the two scene textures per nesting level are reused across
frames and ping-pong so the backdrop stays sampleable while it is written.
Backends that consume neither format — the D3D12/DXIL path, which has no shader
compiler here — keep painting the group with ordinary source-over blending.
`backdrop-filter` still paints nothing.

## Text stack (M4b)

`runtime/ext_gui/text.{h,cpp}` is the front-end; it dispatches through a small
backend table. The default backend is the bundled **stb_truetype**
(`text_stb.cpp`, `runtime/ext_gui/stb_truetype.h`). The optional **`shaping`**
link feature installs `text_hb.cpp`, a HarfBuzz + FreeType backend, and is
selected automatically for non-ASCII text.

- Fonts are resolved from well-known system paths (Helvetica/Arial on macOS,
  DejaVu/Liberation on Linux, Segoe UI/Arial on Windows), overridable with
  `XT_GUI_FONT` (and `XT_GUI_FONT_MONO`), and cached per `(family class, size)`.
  Only regular upright faces are used for now; weight/italic selection is a
  later refinement.
- The default candidates prefer a **CJK-capable TrueType/TTC** face (Microsoft
  YaHei on Windows, PingFang/STHeiti on macOS, Noto Sans CJK/WenQuanYi on Linux)
  before the Latin-only ones. A candidate that stb_truetype cannot parse (for
  example a CFF/OTF collection) is skipped in favour of the next one.
- **Codepoints the primary face lacks fall back to another face.** The engine
  keeps a small fallback chain — the platform's symbol face (Segoe UI Symbol,
  Apple Symbols, Noto Sans Symbols, DejaVu Sans) first, then the other family
  class — and resolves each codepoint to the first face in it that has the
  glyph. The run is shaped in segments, one per face, so a mixed run (Latin
  plus an arrow, box-drawing or geometric shape, or Latin inside CJK) draws
  every glyph instead of dropping the missing ones as `.notdef`. Each shaped
  glyph carries its face (`ShapedGlyph::font`) so the renderer rasterises it
  from the right atlas entry; `xt_text_scale` maps a face to the display's scale
  factor.
- `xt_text_measure_width` maps each codepoint to a glyph and adds its advance
  plus the font's `kern` table; `xt_text_metrics` returns stb_truetype's ascent /
  descent / normal line height. When no font file can be found the module falls
  back to a deterministic per-byte approximation, so layout still works.
- `xt_text_shape_run` returns positioned glyphs and `xt_text_rasterize` renders
  an 8-bit bitmap; the renderer packs those into the atlas. On HiDPI displays
  glyphs are rasterised at `font_size * SDL_GetWindowPixelDensity` while quads
  are positioned in logical pixels, so text stays crisp.
- Layout uses these real metrics for text widths, line breaking and
  `line-height: normal`; `measureText`/`fontMetrics` expose them to tests.

Shape trade-off: the default is a simple shaper, not a full OpenType engine. It
handles Latin/CJK/Cyrillic/Greek, basic kerning and per-codepoint fallback, but
it does not run ligatures or complex-script shaping (Arabic/Indic/Thai), and
stb_truetype only accepts TrueType outlines, not CFF/OTF or variable fonts. The
choice keeps a DOM-only binary near 3 MiB. The optional **`shaping`** link
feature installs the HarfBuzz + FreeType backend instead (about 1.2 MiB) and
restores full OpenType shaping and CFF/OTF/variable-font support; it is selected
automatically for non-ASCII text and can be forced with `--feature shaping`.

## Performance (M20)

The engine is a retained-mode pipeline: a DOM mutation restyles the document,
lays it out, rebuilds the display list and uploads vertex arrays; a frame that
changes nothing does no CPU or GPU work. A document with thousands of elements
is kept interactive by budgeting each stage, and every stage is measured. Set
`XT_GUI_PERF=1` to print one line per frame (or `XT_GUI_PERF=<N>` for an
aggregated line every N frames); `win.stats()` returns the same counters, which
is what `tests/e2e/gui-perf.test.ts` asserts on.

- **Cascade.** An `@media` prelude is evaluated **once per rule** per restyle
  (it used to be parsed once per rule *and* node, which was the largest cost on
  a large document). A sheet without `::before`/`::after` rules skips generated
  content entirely, the declaration candidate list is reused across the
  depth-first walk instead of allocated per element, and `:hover`/`:focus` only
  restyle when the sheet actually has such a rule.
- **Text.** Layout and paint share a bounded shaping cache (`text_cache.cpp`):
  `(face, text) -> glyphs + advance`. It uses two generations of up to 2 MiB of
  glyphs / 8,192 entries; when a generation is full the *spare* one is dropped
  and becomes current, so the hot path has no LRU bookkeeping. A word is shaped
  once and reused by every later layout pass and repaint that draws it. Primary
  face lookups are cached by (family class, size) as well.
- **No-op writes.** `textContent` and `style.<property>` do nothing when the
  node already shows that value: the document is not dirtied, so no restyle,
  relayout, repaint or upload follows. A HUD that assigns the same clock string,
  the same bar width or the same transform every frame therefore costs nothing —
  before this each assignment rebuilt a text node (an allocation, and one the
  collector then had to sweep) or re-serialized the inline style, and dirtied
  the document for a full restyle. `classList` and `removeAttribute` already
  behaved this way; `setAttribute` still dirties unconditionally.
- **Frame loop.** The media/canvas tables are reconciled with the DOM only when
  the document changed (or on the first frame) instead of walking the tree every
  frame. A frame presents only when something happened — a mutation, a scroll,
  an animation frame, a transition, a new video/canvas frame, an expose/resize
  event — so a static page performs no swapchain acquire and no draw at all.
  An expose is the one present the window system asks for rather than the page,
  and `exposes` counts those frames, so `presents - exposes - deferred` stays
  the document's own work even when a window server maps the window late.
  A frame that cannot get a swapchain image (SDL's non-blocking acquire skips a
  frame while the GPU still holds the image, and a minimized window has none)
  keeps its invalidation pending instead of dropping it, so the paint is
  presented on the next frame; those presents are counted as `deferred`.
  `XT_GUI_ALWAYS_PRESENT=1` restores the old always-present behaviour when
  debugging a missed invalidation.
- **Paint and upload.** The display list and the vertex staging arrays are kept
  on the window and cleared per repaint, so their capacity survives. All of a
  dirty frame's vertex/atlas uploads share one command buffer (one submit),
  the geometry's staging transfer buffer is reused (cycled while the previous
  copy may still be in flight), and paint skips the z-index sort and temporary
  vector when no child stacks.
- **Draw.** Consecutive ops with the same pipeline, clip and texture still merge
  into one run, and the renderer now only re-binds a pipeline/vertex
  buffer/scissor when it actually changes. `drawCalls`/`drawMs` in `win.stats()`
  report the run count and its cost; they are the metric for the remaining
  follow-up (an overlap-aware batching pass) — painter order requires
  alternating background and text runs to stay separate until that lands.
- **Immediate-mode surfaces.** `<canvas>` 2D and WebGL draws are script-driven,
  so their cost is per call rather than per document change; the counters above
  cover the retained pipeline only. `tests/e2e/gui-draw-perf.test.ts` measures
  them: on the reference Windows x64/Vulkan machine a 1280×720 frame with 5,000
  solid `fillRect`s, 200 eight-point strokes, 300 `fillText` runs and 200 1:1
  `drawImage`s fell from 231ms to 158ms (fill 4.3ms, stroke 151ms, text 1.4ms,
  image 1.6ms), and 2,000 WebGL `drawElements` calls with a per-draw matrix went
  from 38.6ms to 27.5ms while the render passes opened per frame dropped from
  2,001 to 1 and a 64 KiB `bufferSubData` into a 4 MiB buffer stopped
  re-uploading the whole buffer. A 100×100 canvas redraw now uploads 40 KB
  rather than the whole 1.2 MB bitmap. The suite asserts the counters (one
  upload per frame, no steady-state pipeline creation, only the dirty bytes), so
  the wall-clock numbers stay informational and a software GPU in CI is not
  penalised.

Known limits: a mutation still restyles and relayouts the **whole** document
(there is no incremental subtree invalidation), and a paint-only change (for
example a hover colour) currently reflows too; `win.stats()` is what a follow-up
would be keyed off. Inline `style="…"` attributes are re-parsed on every restyle
of the element that carries them.

As a reference point, `tests/e2e/gui-perf.test.ts` builds a 200-row list (about
1,000 elements, 400 draw runs) and prints the numbers it measured, e.g.
`textFrame=5.2ms restyle=3.6ms layout=1.7ms paint=1.0ms upload=0.8ms draw=0.4ms`
for a one-field `textContent` change on a Windows x64/Vulkan machine. The
absolute values are host-specific; the test asserts the *counters* (one restyle,
one layout, cache hits instead of shapes, no work on an idle frame), which are
what a regression would change.

