# Canvas: `<canvas>` and the 2D context (M14)

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

`<canvas>` is a **replaced element** laid out like `<img>`: its intrinsic size
comes from the `width`/`height` attributes (default `300x150`, the CSS default
canvas size), an explicit CSS `width`/`height` wins, and the aspect ratio is
preserved when only one axis is constrained. Paint emits one `PaintImage` per
canvas, carrying a `canvas_id` instead of a `src`/`media_id`; the renderer
uploads the bitmap to an RGBA texture through the same image path as bitmaps and
video frames (`renderer_image.cpp`), so the GPU scales it into the CSS box.

The engine-side object is `xtgui::Canvas2D`
(`runtime/ext_gui/canvas.{h,cpp}`) — one RGBA8 bitmap plus the 2D drawing state.
The rasteriser lives in `canvas_raster.cpp`, stroke geometry (caps, joins,
dashes) in `canvas_stroke.cpp`, gradients in `canvas_gradient.cpp`, and drawing
state parsing in `canvas_style.cpp`; the DOM bindings live in
`dom_api_canvas.cpp` / `dom_api_canvas_extra.cpp`, and the per-window lifetime in
`gui_canvas.cpp`.

## Getting a context

```ts
const canvas: any = document.getElementById("c");
const ctx = canvas.getContext("2d"); // or "webgl" for a GPU context
```

A canvas has one context kind. `getContext("webgl")` or `getContext("webgl2")`
returns a WebGL context (see [WebGL](./webgl.md)); once a canvas has a 2D context,
asking for either WebGL kind answers `null`, and vice versa. A canvas that never
asked for a context stays transparent.

`getContext("2d")` returns a context handle whose methods and accessors live on
a shared prototype. The handle is cached on the element, so every call returns
the **same** object and the drawing state (transform, styles, path) is per
canvas, not per call. `ctx.canvas` is the owning element handle.

`getContext("2d", attributes)` honours `alpha` on the first call: with
`{ alpha: false }` the bitmap starts (and clears) opaque black. The other
attributes are reported by `getContextAttributes()` at their specification
defaults (`antialias: true`, `depth: true`, `stencil: false`,
`premultipliedAlpha: true`, `preserveDrawingBuffer: false`, `colorSpace:
"srgb"`, …), so a feature test gets an honest answer.

## Drawing model

The rasteriser is deliberately small and self-written (no Skia/Cairo). It
anti-aliases with **4 vertical sub-scanlines** per pixel row and an **analytic
horizontal coverage** pass, and fills paths with the **non-zero winding rule**
(so `arc`-shaped holes work when wound the other way) unless
`fill("evenodd")` selects the even-odd rule. An axis-aligned rectangle
(`b == 0 && c == 0` in the transform) takes a direct coverage fast path.
Blending is non-premultiplied source-over, scaled by `globalAlpha`.

A fill or stroke paints with a solid colour **or a `CanvasGradient`**. Gradient
coordinates are user-space coordinates, so the rasteriser maps each device pixel
back through the inverse of the current transform before sampling; this keeps a
gradient attached to the shape when the shape is translated, scaled or rotated.
Strokes are flattened into segment quads plus the join/cap shapes selected by
`lineJoin`/`lineCap`; a dash pattern first splits the polyline into its "on"
pieces, then those pieces are stroked with the same caps.

Canvas pixels are **not** touched by the garbage collector: a canvas is owned by
the window, keyed by its DOM node (the same shape as the `<video>`/`<audio>`
decode table). It is released when the node is dropped (`loadHTML`, window
close/quit, or a mutation that discards the subtree), which also releases the
matching GPU texture.

## Supported API

State and transforms: `save` / `restore` (which restore the transform, line
width, alpha, font/text settings **and** the fill/stroke styles),
`translate`, `rotate`, `scale`, `transform(a,b,c,d,e,f)` / `transform(matrix)`,
`setTransform(a,b,c,d,e,f)` / `setTransform(matrix)`, `resetTransform`,
`getTransform` (returns an object with `a`…`f` and `is2D`). A matrix is a
`{ a, b, c, d, e, f }` object or a six-element array. Transforms compose in
canvas order: `translate(x, y); rotate(a)` rotates around the translated origin.

Styles and settings: `fillStyle` / `strokeStyle` (CSS colors understood by the
cascade's color parser, or a `CanvasGradient`), `lineWidth`, `globalAlpha`,
`lineCap` (`butt`/`round`/`square`), `lineJoin` (`miter`/`round`/`bevel`),
`miterLimit`, `lineDashOffset`, `font` (a CSS font shorthand resolving to
family, size and bold/italic — default `10px sans-serif`), `textAlign`
(`start`/`left`, `center`, `right`/`end`) and `textBaseline` (`alphabetic`,
`top`, `middle`, `bottom`, `hanging`, `ideographic`). Reading a property returns
the value last assigned; assigning an unparseable color keeps the previous one.

Gradients: `createLinearGradient(x0, y0, x1, y1)` and
`createRadialGradient(x0, y0, r0, x1, y1, r1)` return a `CanvasGradient` with
`addColorStop(offset, color)`. Stops stay live, so a stop added after
`fillStyle = gradient` affects later fills, and the `fillStyle`/`strokeStyle`
getter returns the same gradient handle. Radial gradients interpolate by
distance from the **outer** circle's centre (`x1, y1`), so `x0`/`y0` are
currently ignored: use concentric circles (`x0 == x1`, `y0 == y1`).

Paths: `beginPath`, `closePath`, `moveTo`, `lineTo`, `rect`, `roundRect`,
`arc`, `ellipse`, `quadraticCurveTo`, `bezierCurveTo`, then
`fill(fillRule?)` / `fill(path, fillRule?)` (multi-loop; `"nonzero"` or
`"evenodd"`) or `stroke(path?)`. Bezier and quadratic segments are flattened
into line segments; arcs and ellipses are flattened too. `roundRect` accepts a
single radius number or the CSS radius list (`[all]`, `[tl, br]`,
`[tl, tr, br]`, `[tl, tr, br, bl]`, numbers or `{ x, y }` pairs; corners are
drawn circular).

`Path2D` is implemented with the same path commands (`moveTo`, `lineTo`,
`closePath`, `rect`, `roundRect`, `arc`, `ellipse`, `quadraticCurveTo`,
`bezierCurveTo`, `addPath`) and can be passed to `fill`/`stroke`, so a path can
be built once and replayed under different transforms.

Rectangles: `clearRect`, `fillRect`, `strokeRect`.

Dashes: `setLineDash(segments)` / `getLineDash()` / `lineDashOffset` implement
the standard pattern rules (an odd-length list is duplicated, negative or
non-finite entries are ignored, an all-zero list means a solid line).

Text: `measureText(text)` (returns `{ width }`), `fillText(text, x, y,
maxWidth?)` and `strokeText(text, x, y, maxWidth?)`. Text is shaped with
HarfBuzz and rasterised with FreeType (the same stack as HTML text) at the
transform's approximate scale. A positive `maxWidth` condenses advances and
glyph bitmaps horizontally so the run is at most that wide (as the canvas API
specifies) instead of being ignored.
**Only translation and uniform scale are honoured** — a rotated or skewed
transform still places each glyph at its transformed baseline point but does not
rotate the glyph bitmaps. `strokeText` currently fills with the stroke style
rather than producing an outline.

Images: `drawImage(source, …)` in the 3-argument (`dx, dy`), 5-argument
(`dx, dy, dw, dh`) and 9-argument (`dx, dy, dw, dh, sx, sy, sw, sh`; this
engine's 9-argument form is destination-first) forms. A full affine transform
(rotation/skew/scale) is honoured: the destination rectangle becomes a
parallelogram and every device pixel is inverse-mapped to the source rectangle
(nearest-neighbour). `source` may be an `<img>` element (decoded through the image module) or another
`<canvas>` element (its current bitmap). Sampling is nearest-neighbour, so a
large upscale of a small source is blocky — fine for icons and pixel art, not a
photo pipeline.

Test hook: `getImageData(x, y, w, h)` copies a region to
`{ width, height, data }` where `data` is a row-major RGBA8 byte array.
Out-of-bounds pixels are transparent.

## Not implemented

Patterns (`createPattern`), shadows, `clip()`, `globalCompositeOperation`,
`putImageData`, `createImageData`, `isPointInPath`, `drawImage` with an image
smoothing toggle, `toDataURL`/`toBlob`, `<canvas>` as a CSS `background-image`,
and offscreen canvases (`OffscreenCanvas`, `transferControlToOffscreen`).
Radial gradients ignore the inner circle's offset.

See also [Media](./media.md) for the image/video/audio decode stack that
`drawImage` and the texture upload share.
