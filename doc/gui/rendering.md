# Paint, GPU and text

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

## Paint and display list (M4a/M4b)

`runtime/ext_gui/paint.{h,cpp}` walks the layout tree in painter's order and
emits a backend-agnostic `DisplayList` with two parallel lists: **rectangles**
(backgrounds and four solid border edges, with `border-radius`) and **text runs**
(each carrying its text, colour and resolved `FontSpec`). Keeping them separate
lets the renderer draw all rectangles, then all text on top, with one draw call
per list. `DisplayList::dump()` feeds `paintList()`/`paintCount()`.

`opacity` is honoured only at its extremes: a box whose computed `opacity` is
`0` (and its whole subtree) paints nothing, and anything above `0` paints fully
opaque. A shape carries its colour's alpha only, so there is no compositing of a
partly transparent box — the `opacity` transitions the CSS parser accepts are
therefore not visible either. Hiding and revealing a layer with `opacity: 0` on
the hidden state is what `examples/gui/pelican-bike`'s game-over overlay does.

`runtime/ext_gui/renderer.{h,cpp}` turns that list into two batched vertex
buffers per window (one for shapes, one for glyph quads) drawn through two
SDL_GPU graphics pipelines:

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
  until that is resolved upstream.
- A **rounded-rectangle distance field** in the shape fragment shader gives
  antialiased fills; the vertex carries `position`, `local`, `half extents`,
  `radius` and colour, and a viewport-size push constant does the projection.
  Alpha blending is enabled.
- The text pipeline samples a **single shared grayscale glyph atlas**
  (`R8_UNORM`, 2048², shelf-packed, LINEAR filtering) and draws each glyph as a
  textured quad (`position`, `uv`, colour), modulating alpha by the coverage.
- Geometry is uploaded only when the document or viewport changes
  (`geometry.dirty`), so steady-state frames are bind-and-draw.

The window background (`setBackground`) is the render-pass clear colour.

Still to do in M4: gradients.

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

