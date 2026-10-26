# WebGL 2 instanced cubes

64 lit, textured cubes rendered with **one instanced draw call** through a
WebGL 2 context. `scene.js` uses:

- `#version 300 es` shaders with `in`/`out` and a named fragment output;
- one **vertex-array object** holding four vertex buffers and the index buffer;
- a per-instance `vec4` attribute (`xyz` offset, `w` phase) with
  `vertexAttribDivisor(…, 1)`;
- `drawElementsInstanced` for the whole 64-cube grid;
- a mipmapped procedural checker texture.

The page composites the context's colour texture directly — geometry, lighting,
texture sampling and blending all happen on the GPU, with no readback.

`index.html` loads `scene.js` as a plain classic script, so the same document
runs unchanged in a browser. `main.ts` opens the window, loads the page and
prints a `label=value` report with `readPixels` probes.

```bash
npx tsx src/cli/main.ts run examples/gui/webgl2-cubes/main.ts
npx tsx src/cli/main.ts build examples/gui/webgl2-cubes/main.ts
```

Expected report (the exact pixels depend on the rotation):

```
window=800x640 (gui extension, SDL_GPU)
gpu=vulkan
webgl2=true
canvas=800x640
instances=64
frames=3
error=0
pixel-center=10,15,25,255
pixel-max=199,199,199,255
```

Set `XT_GUI_AUTOCLOSE_MS=<ms>` to close the window after a delay. On an SDL_GPU
backend that cannot consume SPIR-V (Metal, Direct3D 12) `getContext("webgl2")`
returns `null` and `webgl2=false`; see
[WebGL](../../../doc/gui/webgl.md) for the supported API and limitations.
