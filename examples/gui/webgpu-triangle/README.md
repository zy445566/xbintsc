# WebGPU triangle

A WGSL triangle drawn through **`navigator.gpu`** — the gui extension's WebGPU
implementation, rendered on SDL_GPU (Vulkan on Windows/Linux, Metal on macOS).
The vertex/fragment pair is translated from WGSL to Vulkan GLSL and compiled by
the bundled glslang; the page composites the canvas's colour texture directly,
so no pixels are read back for display.

`main.ts` opens the window, requests an adapter/device, configures the canvas,
uploads a uniform buffer, creates the render pipeline and bind group, and draws
one triangle. It also reads the centre pixel back through
`copyTextureToBuffer` to prove the draw reached the canvas.

```bash
npx tsx src/cli/main.ts run examples/gui/webgpu-triangle/main.ts
npx tsx src/cli/main.ts build examples/gui/webgpu-triangle/main.ts
```

Expected report:

```
webgpu=true
driver=vulkan
format=bgra8unorm
messages=0
center=<r>,<g>,<b>,255
ok=true
```

The exact centre colour depends on which triangle colour the sample lands on.
See [WebGPU](../../../doc/gui/webgpu.md) for the supported API and its limits.
