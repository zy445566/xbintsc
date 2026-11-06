# WebGPU (`navigator.gpu` on a `<canvas>`)

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

A page can create a WebGPU device and draw into a `<canvas>`:

```ts
const canvas: any = document.getElementById("scene");
const context: any = canvas.getContext("webgpu");
const gpu: any = (navigator as any).gpu;
const adapter = await gpu.requestAdapter();
const device = await adapter.requestDevice();
context.configure({ device, format: gpu.getPreferredCanvasFormat() });

const module = device.createShaderModule({ code: WGSL });
// … pipeline, bind group, command encoder, render pass …
```

The implementation sits behind the optional **`webgpu` link feature** (which
requires `gpu`, the bundled glslang). A page that never touches WebGPU links
none of it: `getContext("webgpu")` answers `null`, `navigator.gpu` is
`undefined`, and the `GPU…` host types do not exist, so a `typeof` feature test
takes its fallback path.

## How a frame reaches the screen

- `context.getCurrentTexture()` returns a `GPUTexture` whose SDL_GPU texture is
  also the canvas's colour texture. The page's image pipeline samples it
  directly (no CPU readback), exactly like the WebGL path.
- `context.configure()` creates the backbuffer at the canvas's `width` /
  `height`; the `canvas.width` / `canvas.height` setters recreate it.
- Queue submissions are immediate SDL_GPU command buffer submissions. A
  completed render pass is visible to the compositor on the next frame.
- Buffers used as **uniform buffers** keep a CPU mirror: SDL_GPU takes uniform
  data through `SDL_Push…UniformData`, not a bound buffer. `writeBuffer`
  updates both the mirror and (when the buffer has GPU usage) the SDL buffer.

## WGSL

WGSL is translated to Vulkan-dialect GLSL, then compiled by the same bundled
glslang that serves WebGL; on Metal the SPIR-V is cross-compiled to MSL by the
bundled SPIRV-Cross. The front end (`runtime/ext_gui/wgsl_*.cpp`) supports:

- `struct`, `alias`, `const`, `override`, module-scope `var` in `private`,
  `workgroup`, `uniform` and `storage` address spaces;
- `@vertex`, `@fragment`, `@compute` entry points with `@location`,
  `@builtin(position|vertex_index|instance_index|front_facing|frag_depth|
  sample_index|sample_mask|local_invocation_id|global_invocation_id|
  workgroup_id|num_workgroups|local_invocation_index)`, `@interpolate`,
  `@group`/`@binding` and `@workgroup_size`;
- the scalar/vector/matrix/array constructors, swizzles, `let`/`var`,
  `if`/`else`, `for`, `while`, `loop`/`continuing`, `switch`, `break`,
  `continue`, `return`, `discard`, and the usual math/texture builtins;
- `texture_2d`/`texture_2d_array`/`texture_3d`/`texture_cube` (and the depth
  variants), `sampler`, `sampler_comparison`, `texture_storage_2d`, plus
  `textureSample*`, `textureLoad`, `textureStore`, `textureDimensions` and
  `textureGather`.

A texture and a sampler are separate WGSL bindings but SDL_GPU binds them as a
pair, so the translator analyses the `textureSample` call sites and emits one
combined sampler per (texture, sampler) pair. A texture used only through
`textureLoad` gets a slot with a default sampler.

## Supported API

Implemented interfaces: `GPU`, `GPUAdapter`, `GPUAdapterInfo`, `GPUDevice`,
`GPUQueue`, `GPUBuffer`, `GPUTexture`, `GPUTextureView`, `GPUSampler`,
`GPUShaderModule`, `GPUBindGroupLayout`, `GPUPipelineLayout`, `GPUBindGroup`,
`GPURenderPipeline`, `GPUComputePipeline`, `GPUCommandEncoder`,
`GPURenderPassEncoder`, `GPUComputePassEncoder`, `GPURenderBundleEncoder`,
`GPURenderBundle`, `GPUQuerySet`, `GPUCanvasContext`, `GPUCompilationInfo`,
`GPUCompilationMessage` and the `GPUBufferUsage` / `GPUTextureUsage` /
`GPUShaderStage` / `GPUMapMode` / `GPUColorWrite` namespaces.

That includes device creation, error scopes (`pushErrorScope` /
`popErrorScope`) and `uncapturederror`, buffer mapping
(`mapAsync`/`getMappedRange`/`unmap`), `queue.writeBuffer`/`writeTexture`,
render and compute pipelines (including the `*Async` variants), bind groups,
render/compute passes, draws (indexed, indirect, instanced), compute dispatches
(direct and indirect), `copyBufferToBuffer`, `copyBufferToTexture`,
`copyTextureToBuffer`, `copyTextureToTexture`, `clearBuffer` and a
basic render-bundle replay.

## Not implemented

These parts of the specification are accepted by the API but report a
validation error (or do nothing observable) because SDL_GPU has no equivalent:

- **Queries**: `GPUQuerySet` exists, but `beginOcclusionQuery`,
  `endOcclusionQuery`, `resolveQuerySet` and `writeTimestamp` record a
  validation error; the SDL_GPU build has no query API.
- **`copyExternalImageToTexture` / `importExternalTexture`**: DOM image/canvas
  sources are not bridged into the WebGPU queue.
- **Atomics** (`atomic<u32>` and the `atomic*` builtins) are rejected by the
  WGSL front end.
- **Storage-texture writes in fragment stages**, MSAA resolve targets beyond
  SDL's color-target resolve, and non-`2d` canvas contexts.
- **Pointers** in user functions (`ptr<…>` parameters and `&`/`*` on locals).
- A pipeline layout uses at most **4 uniform buffers per stage** and **16
  sampled textures per stage** (SDL_GPU's per-stage limits); a shader that
  exceeds them fails validation with an explanatory message.
- `GPUTexture.createView()` accepts its descriptor but SDL_GPU binds a whole
  texture, so `baseMipLevel` / `arrayLayerCount` selection is not represented.
- A bind group applies the first dynamic offset to every dynamic entry; the
  common one-offset-per-group pattern works.

Direct3D 12 is not usable: SDL_GPU's D3D12 backend consumes DXIL, which this
build has no compiler for. On Windows the GUI engine picks Vulkan when it is
available (the same rule as WebGL).

## Testing

`tests/e2e/gui-webgpu.test.ts` draws a textured triangle with a WGSL
vertex/fragment pair, reads the canvas back through `copyTextureToBuffer`,
runs a compute shader over a storage buffer and checks an error scope and a
shader compilation message. Demo: `examples/gui/webgpu-triangle`.
