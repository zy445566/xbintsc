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

`OffscreenCanvas` has the same 2D surface without a DOM element:
`new OffscreenCanvas(width, height)` allocates a bitmap owned by the window,
`getContext("2d")` returns a context whose `canvas` is the offscreen object, and
`canvas.transferControlToOffscreen()` hands a live element's bitmap to one
(`element.getContext(…)` then answers `null`). `drawImage`, `createPattern`,
`createImageBitmap` and `texImage2D` accept an `OffscreenCanvas` as a source.
`transferToImageBitmap()` returns an `ImageBitmap` and `convertToBlob()`
resolves to the same PNG blob shape as `toBlob` (there is no worker or
`OffscreenCanvas` WebGL context in this runtime).

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
Blending uses non-premultiplied source-over by default. `globalCompositeOperation`
selects the full canvas set: the Porter-Duff operators (`source-over`,
`source-in`, `source-out`, `source-atop`, `destination-over`,
`destination-in`, `destination-out`, `destination-atop`, `lighter`, `copy`,
`xor`) and the separable/non-separable blend modes (`multiply`, `screen`,
`overlay`, `darken`, `lighten`, `color-dodge`, `color-burn`, `hard-light`,
`soft-light`, `difference`, `exclusion`, `hue`, `saturation`, `color`,
`luminosity`). Coverage from anti-aliasing and the clip region interpolates
between the untouched destination and the composited result, so a partially
covered pixel is correct for every operator.

`clip()` intersects the current clip region with the current path (or a
`Path2D` and an optional `"nonzero"`/`"evenodd"` rule). The region is stored as
a device-space coverage mask, so it follows the transform in effect when
`clip()` was called; `save`/`restore` restore it and resizing the bitmap clears
it. Every fill, stroke, text run, image draw and `clearRect` honours the mask.

Shadows (`shadowColor`, `shadowBlur`, `shadowOffsetX`, `shadowOffsetY`) are
painted from the operation's own alpha: the draw is first rendered into a
scratch layer, its alpha is offset and box-blurred (three separable passes),
tinted with `shadowColor`, composited, and the unfiltered layer is drawn over
it. The offset and blur are in canvas (device) units and are not affected by
the transform, as the API specifies. `clearRect` and `putImageData` never cast
a shadow.

`isPointInPath(x, y[, fillRule])`, `isPointInPath(path, x, y[, fillRule])`,
`isPointInStroke(x, y)` and `isPointInStroke(path, x, y)` hit-test the current
path (or the supplied `Path2D`) under the current transform; stroke hit testing
uses the device-space distance to each flattened segment and the current line
width.

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
`globalCompositeOperation`, `shadowColor` / `shadowBlur` / `shadowOffsetX` /
`shadowOffsetY`, `imageSmoothingEnabled` / `imageSmoothingQuality`,
`lineCap` (`butt`/`round`/`square`), `lineJoin` (`miter`/`round`/`bevel`),
`miterLimit`, `lineDashOffset`, `font` (a CSS font shorthand resolving to
family, size and bold/italic — default `10px sans-serif`), `textAlign`
(`start`/`left`, `center`, `right`/`end`) and `textBaseline` (`alphabetic`,
`top`, `middle`, `bottom`, `hanging`, `ideographic`). Reading a property returns
the value last assigned; assigning an unparseable color keeps the previous one.
`imageSmoothingEnabled` defaults to `true` (bilinear sampling);
`imageSmoothingQuality` reports `low`, `medium` or `high` and selects the same
bilinear filter in this backend.

Gradients: `createLinearGradient(x0, y0, x1, y1)` and
`createRadialGradient(x0, y0, r0, x1, y1, r1)` return a `CanvasGradient` with
`addColorStop(offset, color)`. Stops stay live, so a stop added after
`fillStyle = gradient` affects later fills, and the `fillStyle`/`strokeStyle`
getter returns the same gradient handle. A radial gradient is the general
two-circle cone: the colour at a point is resolved from the interpolated circle
(`centre = c0 + t·(c1 − c0)`, `radius = r0 + t·(r1 − r0)`), so an offset inner
circle and different radii behave like a browser's. A point inside the inner
circle samples stop 0.

Paths: `beginPath`, `closePath`, `moveTo`, `lineTo`, `rect`, `roundRect`,
`arc`, `ellipse`, `quadraticCurveTo`, `bezierCurveTo`, then
`fill(fillRule?)` / `fill(path, fillRule?)` (multi-loop; `"nonzero"` or
`"evenodd"`), `stroke(path?)`, `clip(path?, fillRule?)` and the
`isPointInPath`/`isPointInStroke` hit tests. Bezier and quadratic segments are
flattened into line segments; arcs and ellipses are flattened too. `roundRect`
accepts a single radius number or the CSS radius list (`[all]`, `[tl, br]`,
`[tl, tr, br]`, `[tl, tr, br, bl]`, numbers or `{ x, y }` pairs; corners are
drawn circular).

`Path2D` is implemented with the same path commands (`moveTo`, `lineTo`,
`closePath`, `rect`, `roundRect`, `arc`, `ellipse`, `quadraticCurveTo`,
`bezierCurveTo`, `addPath`) and can be passed to `fill`/`stroke`/`clip` and the
`isPointIn*` hit tests, so a path can be built once and replayed under different
transforms.

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
parallelogram and every device pixel is inverse-mapped to the source rectangle.
`source` may be an `<img>` element (decoded through the image module), another
`<canvas>`, an `OffscreenCanvas`, or an `ImageBitmap`. Sampling is bilinear when
`imageSmoothingEnabled` is `true` (the default) and nearest-neighbour when it is
`false`.

Test hook: `getImageData(x, y, w, h)` copies a region to
`{ width, height, data }` where `data` is a row-major RGBA8 byte array.
Out-of-bounds pixels are transparent.

Patterns: `createPattern(source, repetition?)` accepts an `<img>`,
`<canvas>`, `OffscreenCanvas` or `ImageBitmap` and tiles it in user space
(`repeat`, `repeat-x`, `repeat-y`, `no-repeat`), so the tiles follow the
current transform. Assign the result to `fillStyle`/`strokeStyle`; assigning a
colour replaces it.

Image data and export: `createImageData(width, height)` allocates a blank
`{ width, height, data }`, and `putImageData(imageData, dx, dy[, dirtyX, dirtyY,
dirtyWidth, dirtyHeight])` copies bytes straight into the bitmap (no transform,
blending, clip or `globalAlpha`). `canvas.toDataURL(type?)` exports a PNG data
URL and `canvas.toBlob(callback, type?)` delivers a Blob-shaped `{ type, size }`
from a microtask (the bytes are on `__xt_gui_blob_data`; there is no
`URL.createObjectURL`). `OffscreenCanvas.convertToBlob()` resolves to the same
shape and `transferToImageBitmap()` snapshots the bitmap.

CSS backgrounds: `background-image: url(...)` accepts a file path or a `data:`
URL, so a canvas can be used as a CSS background by exporting it first
(`element.style.backgroundImage = "url(" + canvas.toDataURL() + ")"`). An image
layer is stretched to the element's border box; `background-size`,
`background-position` and tiling are not modelled.

## Not implemented

Smoothing quality has one filter (bilinear) on this backend. Offscreen canvases
support the 2D context only (a WebGL context needs a DOM element for its
texture table). Text under a rotated or skewed transform places glyphs on the
transformed baseline but does not rotate the glyph bitmaps, and `strokeText`
fills with the stroke style rather than outlining.
See also [Media](./media.md) for the image/video/audio decode stack that
`drawImage` and the texture upload share.
