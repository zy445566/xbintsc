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

`opacity` is honoured only at its extremes: a box whose computed `opacity` is
`0` (and its whole subtree) paints nothing, and anything above `0` paints fully
opaque. A shape carries its colour's alpha only, so there is no compositing of a
partly transparent box — the `opacity` transitions the CSS parser accepts are
therefore not visible either. Hiding and revealing a layer with `opacity: 0` on
the hidden state is what `examples/gui/pelican-bike`'s game-over overlay does.

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

- a **linear** gradient is sampled at 18 points and emitted as solid rectangles;
  `to <side>`, `deg`/`grad`/`rad`/`turn`, percentage and multi-stop colours work
  (the band walk is axis-aligned, so an oblique angle uses the nearest axis);
- a **radial** gradient is 32 concentric ellipses. Each is a unit-radius rounded
  rectangle mapped through the shape pipeline's transform slot, so a circle or
  ellipse keeps the distance-field antialiasing. Shape/size keywords
  (`circle`/`ellipse`, `closest-side`/`farthest-side`/`closest-corner`/
  `farthest-corner`, explicit radii) and `at <position>` are parsed.

The background colour sits under every layer. Gradients are partially
transparent if their stops are, and the band approximation can show banding and
alpha stacking on a very translucent multi-stop gradient.

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
the single render pass — there is no offscreen render target — at the cost of
group semantics: overlapping descendants of a blurred/filtered group can darken,
and `backdrop-filter` cannot sample what is behind the element, so it parses and
cascades but paints nothing.

## Text stack (M4b)

`runtime/ext_gui/text.{h,cpp}` wraps **HarfBuzz** (shaping) and **FreeType**
(metrics + eventual rasterisation). Both are built statically and linked into
`gui.a` by `scripts/build-gui.ts`.

- Fonts are resolved from well-known system paths (Helvetica/Arial on macOS,
  DejaVu/Liberation on Linux, Segoe UI/Arial on Windows), overridable with
  `XT_GUI_FONT` (and `XT_GUI_FONT_MONO`), and cached per `(family class, size)`.
  Only regular upright faces are used for now; weight/italic selection is a
  later refinement.
- The default candidates prefer a **CJK-capable** face (Microsoft YaHei on
  Windows, PingFang/STHeiti on macOS, Noto Sans CJK/WenQuanYi on Linux) before
  the Latin-only ones. Setting `XT_GUI_FONT` to a Latin-only face gives its
  Latin design instead (at the cost of CJK coverage).
- **Codepoints the primary face lacks fall back to another face.** The engine
  keeps a small fallback chain — the platform's symbol face (Segoe UI Symbol,
  Apple Symbols, Noto Sans Symbols, DejaVu Sans) first, then the other family
  class — and resolves each codepoint to the first face in it that has the
  glyph. The run is shaped in segments, one per face, so a mixed run (Latin
  plus an arrow, box-drawing or geometric shape, or Latin inside CJK) draws
  every glyph instead of dropping the missing ones as `.notdef`. This is what
  makes a character such as `▮`/`▯` (U+25AE/U+25AF, used by the pelican-bike
  speed bar) visible even when the CJK primary has no geometric shapes. Each
  shaped glyph carries its face (`ShapedGlyph::font`) so the renderer rasterises
  it from the right atlas entry; `xt_text_scale` maps a face to the display's
  scale factor.
- `xt_text_measure_width` shapes the run with HarfBuzz (so kerning and
  ligatures are honoured), `xt_text_metrics` returns FreeType's ascent /
  descent / normal line height. When no font file can be found the module falls
  back to a deterministic per-byte approximation, so layout still works.
- `xt_text_shape_run` returns positioned glyphs and `xt_text_rasterize` renders
  an 8-bit bitmap; the renderer packs those into the atlas. On HiDPI displays
  glyphs are rasterised at `font_size * SDL_GetWindowPixelDensity` while quads
  are positioned in logical pixels, so text stays crisp.
- Layout uses these real metrics for text widths, line breaking and
  `line-height: normal`; `measureText`/`fontMetrics` expose them to tests.

