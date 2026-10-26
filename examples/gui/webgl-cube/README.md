# WebGL cube

A spinning, textured, directionally lit cube rendered with **WebGL 1.0** — no
CPU rasterisation and no readback. `scene.js` compiles its GLSL ES shaders at
run time (the engine translates them to SPIR-V with a bundled glslang), uploads
three vertex buffers and a 16-bit index buffer, generates a checker texture with
mipmaps, and draws 36 indices every frame. The page composites the canvas's
colour texture directly.

`index.html` loads `scene.js` as a plain classic script, so the same document
runs unchanged in a browser. `main.ts` is the native host: it opens the window,
loads the document, and prints a `label=value` report with pixel probes read
back through `gl.readPixels`.

```bash
npx tsx src/cli/main.ts run examples/gui/webgl-cube/main.ts
npx tsx src/cli/main.ts build examples/gui/webgl-cube/main.ts
```

Expected report (the exact pixels depend on the rotation):

```
window=800x640 (gui extension, SDL_GPU)
gpu=vulkan
webgl=true
canvas=800x640
frames=3
error=0
pixel-center=22,22,22,255
pixel-max=126,126,126,255
```

`gpu` is `win.driver()`: SDL_GPU picks Vulkan on Windows, which is the backend
the runtime GLSL compiler targets. `error=0` is `gl.getError()` after the last
draw, and `pixel-max` is the brightest pixel across the cube's box — a lit face
rather than the dark background.

Set `XT_GUI_AUTOCLOSE_MS=<ms>` to close the window after a delay. On an SDL_GPU
backend that cannot consume SPIR-V (Metal, Direct3D 12) `getContext("webgl")`
returns `null` and `webgl=false`; see
[WebGL](../../../doc/gui/webgl.md) for the supported API and limitations.
