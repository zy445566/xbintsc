# WebGL (a `<canvas>` drawn on the GPU)

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

A `<canvas>` can ask for a WebGL context instead of the 2D one:

```ts
const canvas: any = document.getElementById("scene");
const gl: any = canvas.getContext("webgl2"); // or "webgl" / "experimental-webgl"
if (gl == null) {
  // no GPU backend that can consume WebGL's shaders on this platform
}
```

Unlike the 2D context, which rasterises into a CPU bitmap and uploads it as a
texture, a WebGL context **renders on the GPU**: vertex transform, lighting,
texture sampling, depth testing and blending all execute in the SDL_GPU
pipeline (Vulkan on Windows/Linux, Metal on macOS). The page composites the
context's colour texture directly, so no pixels are read back to the CPU for
display.

Both **WebGL 1.0** and **WebGL 2.0** are available. A canvas keeps the first
context kind it asks for: after `getContext("webgl2")` a later
`getContext("webgl")` answers `null`, and the other way round. A WebGL 2 context
has its own `WebGL2RenderingContext` prototype (with a copy of the WebGL 1
constants and methods), so `gl instanceof WebGL2RenderingContext` is true while
`gl instanceof WebGLRenderingContext` is false, like a browser — libraries such
as three.js use exactly that check to pick their WebGL 2 path. `gl.constructor.name`
reports the type as well.

## How a frame reaches the screen

- Each context owns one offscreen `R8G8B8A8_UNORM` colour texture (the drawing
  buffer) and a `D32_FLOAT` depth texture, sized from the canvas's `width` /
  `height` attributes. `canvas.width = …` recreates them and discards the
  contents, like the DOM rule.
- The context records into its own SDL_GPU command buffer. `clear`/`draw*`
  keep a render pass open; a buffer/texture upload or a framebuffer change ends
  it and starts a new pass with `LOAD`, so the drawing buffer persists between
  draws just like WebGL's.
- Once per frame, before the page's own render pass, the engine submits every
  WebGL command buffer. The image pipeline then samples the colour texture as
  an ordinary `PaintImage`, with no CPU copy.
- WebGL's origin is bottom-left while SDL_GPU render targets are top-left, so
  the framebuffer is stored top-down: `viewport`/`scissor` are converted,
  `readPixels` flips rows back, and the compositor sees an upright texture.
- A fixed-function state change (`blendFunc`, `depthFunc`, `cullFace`,
  `colorMask`, …) changes which `SDL_GPUGraphicsPipeline` is needed, so
  pipelines are created lazily per (program, vertex layout, state) and cached on
  the program.

## Shaders: GLSL ES to SPIR-V (and on to MSL)

SDL_GPU cannot compile GLSL at run time, and each backend consumes its own
format, so the engine ships two statically linked translators:

- **glslang** compiles the rewritten GLSL to SPIR-V, which the Vulkan backend
  consumes directly;
- **SPIRV-Cross** cross-compiles that same SPIR-V to Metal Shading Language 2.0
  text (needed for arrays of textures/samplers), which the Metal backend
  compiles at run time.

Both live in `gui.a` / `gui.lib`; the Metal path reuses the SPIR-V module rather
than growing a second GLSL front end, so both backends accept exactly the same
shader subset. The path is:

1. `shaderSource` stores the GLSL ES text; `compileShader` translates it once
   for `COMPILE_STATUS` / `getShaderInfoLog`.
2. `linkProgram` translates both stages again with one shared attribute and
   varying location map, compiles them with glslang, and then creates the
   `SDL_GPUShader` objects: from the SPIR-V on Vulkan, or from the MSL text
   SPIRV-Cross produced on Metal.
3. The generated SPIR-V is reflected to obtain the exact std140 offsets, array
   strides and matrix strides of the uniform block — the shader text may size an
   array with a macro, but glslang has already resolved it. SPIRV-Cross keeps
   those offsets in the MSL struct, so one uniform layout serves both backends.

The translator (`runtime/ext_gui/webgl_glsl*.cpp`) handles GLSL ES 1.00 and
3.00 sources:

| WebGL spelling | Rewritten to |
| --- | --- |
| `attribute vec3 a_pos` | `layout(location = N) in vec3 a_pos` |
| `varying vec2 v_uv` | `layout(location = N) out/in vec2 v_uv` (one shared name → location map) |
| `gl_FragColor` | `layout(location = 0) out highp vec4 xt_FragColor` |
| `texture2D`, `textureCube`, `texture2DProj` | `texture`, `texture`, `textureProj` |
| `texture2DLodEXT` / `textureCubeLodEXT` | `textureLod` |
| non-sampler uniforms | one `layout(std140) uniform XtUniforms { … }` block |
| `uniform sampler2D u_tex` | `layout(set = 2, binding = N) uniform sampler2D u_tex` (fragment) or set 0 (vertex) |
| `gl_FragData[0]` | `xt_FragColor` |
| `gl_FragDepth` / `gl_FragDepthEXT` | kept (SPIR-V `DepthReplacing`; MSL `[[depth(any)]]`) |
| `sampler2DShadow` / `samplerCubeShadow` / `sampler2DArrayShadow` | kept; bound as an SDL_GPU comparison sampler |
| a missing `#version` / `#version 100` | `#version 310 es` |

Non-sampler uniforms are merged across both stages and emitted as one block in
each stage (set 1 for the vertex shader, set 3 for the fragment shader), which
matches SDL_GPU's Vulkan descriptor layout. On Metal the same declarations
become `[[buffer(0)]]` and `[[texture(N)]]`/`[[sampler(N)]]`, the slots SDL_GPU's
Metal backend binds. `uniformMatrix4fv` writes column-major floats into the
reflected offsets; `uniform1i` on a sampler stores the texture unit. The block
is pushed with the dynamic uniform-buffer API on every draw.

Limits of the translator: struct-typed uniforms, user uniform blocks, uniform
arrays whose size is not resolved by the preprocessor, and a sampler array
indexed by a non-constant expression are rejected (the link/shader info log
says so). A `#pragma unroll_loop_start` loop is expanded before rewriting, so
the `sampler2D shadowMap[ NUM_DIR_LIGHT_SHADOWS ]` / `shadowMap[ i ]` shape
three.js emits becomes constant-indexed. Shader code inside `#ifdef` is fine;
non-sampler uniform declarations are moved to the enclosing top-level position
so the block stays unconditional.

A fragment shader may write `gl_FragDepth`; glslang marks the SPIR-V module
`DepthReplacing` and SPIRV-Cross emits `[[depth(any)]]`, so the write reaches the
depth attachment on both backends. The comparative samplers
(`sampler2DShadow`, `samplerCubeShadow`, `sampler2DArrayShadow`) use the same
SDL_GPU sampler objects with `enable_compare` set; `TEXTURE_COMPARE_MODE` and
`TEXTURE_COMPARE_FUNC` are read from the texture (or from a bound sampler
object in WebGL 2).

### GLSL ES 3.00 (WebGL 2)

A `#version 300 es` source is accepted as-is apart from the version bump:
top-level `in`/`out` declarations become attributes/varyings, a named fragment
output (`out vec4 fragColor;`) becomes an `xt_FragColor`-style output, and
`texture()` / `textureLod()` already match the Vulkan dialect. Multiple fragment
outputs are given sequential locations, but the render pass has one colour
target, so only the first is written. `gl_VertexID` and `gl_InstanceID` are
available from GLSL ES 3.00, which is what the instanced example uses.

## Supported API

- **Context**: `getContext("webgl")`, `canvas`, `drawingBufferWidth/Height`,
  `getParameter`, `getError`, `getSupportedExtensions`, `getExtension`,
  `getContextAttributes`, `isContextLost`, `getShaderPrecisionFormat`, and an
  8-bit stencil buffer (`getContextAttributes().stencil` is `true`,
  `STENCIL_BITS` is 8). `WEBGL_lose_context`'s `loseContext`/`restoreContext`
  really drop and rebuild the context (every object becomes invalid) and
  dispatch `webglcontextlost`/`webglcontextrestored`.
- **Shaders and programs**: `createShader`, `shaderSource`, `compileShader`,
  `getShaderParameter`, `getShaderInfoLog`, `getShaderSource`, `deleteShader`,
  `createProgram`, `attachShader`, `detachShader`, `linkProgram`,
  `getProgramParameter`, `getProgramInfoLog`, `getAttachedShaders`,
  `validateProgram`, `deleteProgram`, `useProgram`,
  `getActiveUniform`/`getActiveAttrib`, `getUniformLocation`,
  `getAttribLocation`, `bindAttribLocation`.
- **Uniforms**: `uniform1f`…`uniform4f`, `uniform1i`…`uniform4i`,
  `uniform1fv`…`uniform4fv`, `uniform1iv`…`uniform4iv`,
  `uniformMatrix2fv`/`3fv`/`4fv`, `getUniform`.
- **Buffers and attributes**: `createBuffer`, `bindBuffer`, `bufferData`,
  `bufferSubData`, `deleteBuffer`, `getBufferParameter`,
  `enableVertexAttribArray`, `disableVertexAttribArray`,
  `vertexAttribPointer`, `vertexAttrib1f`…`4f` / `1fv`…`4fv`,
  `getVertexAttrib`. The `usage` hint from `bufferData`
  (`STREAM_DRAW`/`STATIC_DRAW`/`DYNAMIC_DRAW`) is recorded and reported by
  `getBufferParameter(…, BUFFER_USAGE)`.
- **Drawing**: `drawArrays`, `drawElements` (UNSIGNED_BYTE, UNSIGNED_SHORT,
  UNSIGNED_INT; `TRIANGLE_FAN` and `LINE_LOOP` are expanded to lists).
  `POINTS`, `LINES`, `LINE_STRIP`, `TRIANGLES` and `TRIANGLE_STRIP` map to the
  matching SDL primitive.
- **Textures**: `createTexture`, `bindTexture`, `activeTexture`,
  `texParameteri`/`texParameterf`, `getTexParameter`, `texImage2D` (raw pixels,
  `<img>`, `<canvas>` and `ImageData` sources), `texSubImage2D`,
  `generateMipmap`, `deleteTexture`. `UNPACK_FLIP_Y_WEBGL` and
  `UNPACK_PREMULTIPLY_ALPHA_WEBGL` are honoured. By default source data is
  converted to RGBA8; `RGBA`, `RGB`, `LUMINANCE`, `ALPHA` and
  `LUMINANCE_ALPHA` with `UNSIGNED_BYTE` are accepted. Float textures
  (`RGBA32F`, `RGBA16F`, `RGB32F`, `RGB16F`, `RG32F`/`RG16F`, `R32F`/`R16F`
  with `FLOAT` or `HALF_FLOAT` data, plus WebGL 1's
  `OES_texture_float`/`OES_texture_half_float`) map to SDL's RGBA32F/RGBA16F,
  and depth textures (`DEPTH_COMPONENT16/24/32F`, `DEPTH_STENCIL`,
  `DEPTH24_STENCIL8`) map to `D32_FLOAT`/`D32_FLOAT_S8_UINT` and can be sampled
  or used as depth attachments. Compressed textures upload through
  `compressedTexImage2D`/`compressedTexSubImage2D` with
  `WEBGL_compressed_texture_s3tc` (BC1/BC2/BC3),
  `WEBGL_compressed_texture_s3tc_srgb` and `WEBGL_compressed_texture_astc`;
  each extension is published only when the device can sample its formats.
  `texImage2D` also accepts the latest decoded `<video>` frame and an
  `ImageBitmap` produced by `createImageBitmap`. Cube maps
  (`bindTexture(TEXTURE_CUBE_MAP)` plus six `texImage2D` faces) and 3D/array
  textures (`texImage3D`/`texStorage3D`) are sampled through `samplerCube`,
  `sampler3D` and `sampler2DArray`; `texSubImage3D` updates a sub-volume and
  `framebufferTextureLayer` renders one array/cube layer as a colour
  attachment.
- **Framebuffers**: `createFramebuffer`, `bindFramebuffer`
  (`FRAMEBUFFER`/`READ_FRAMEBUFFER`/`DRAW_FRAMEBUFFER`),
  `framebufferTexture2D` (`COLOR_ATTACHMENT0`, `DEPTH_ATTACHMENT`,
  `DEPTH_STENCIL_ATTACHMENT`), `checkFramebufferStatus`, `createRenderbuffer`,
  `bindRenderbuffer`, `renderbufferStorage` (depth or depth-stencil),
  `framebufferRenderbuffer`, `delete*`, and `blitFramebuffer` (colour through
  SDL's blit; depth through a sampling pass that writes `gl_FragDepth`; stencil
  bits are accepted but not copied). This is enough for render-to-texture,
  shadow maps and post-processing passes.
- **State**: `enable`/`disable`/`isEnabled` for `BLEND`, `DEPTH_TEST`,
  `CULL_FACE`, `SCISSOR_TEST` and `STENCIL_TEST`,
  `clearColor`/`clearDepth`/`clearStencil`, `clear` (with `SCISSOR_TEST` it
  clears only the scissor rectangle, for colour, depth and stencil), `blendColor`,
  `blendFunc`/`blendFuncSeparate`,
  `blendEquation`/`blendEquationSeparate`, `depthFunc`,
  `depthMask`, `colorMask`, `stencilFunc`/`stencilFuncSeparate`,
  `stencilOp`/`stencilOpSeparate`, `stencilMask`/`stencilMaskSeparate`,
  `cullFace`, `frontFace`, `viewport`, `scissor`,
  `pixelStorei`, `finish`. The stencil test takes part in rasterisation and
  `clear(STENCIL_BUFFER_BIT)` clears the stencil attachment; SDL_GPU exposes
  one reference and one compare/write mask for both faces, so a front/back
  pair that differs uses the front value.
- **Readback**: `readPixels` (RGBA/UNSIGNED_BYTE) from the default framebuffer
  or a bound RGBA8 framebuffer; a float/depth attachment is sampled into an
  RGBA8 target first. It blocks until the GPU finishes; use it for tests and
  screenshots, not per frame.

WebGL 2 adds:

- **Vertex-array objects**: `createVertexArray`, `bindVertexArray`,
  `deleteVertexArray`, `isVertexArray`. SDL_GPU has no VAO, so binding one
  restores a saved copy of the attribute table and its element buffer.
- **Instancing**: `drawArraysInstanced`, `drawElementsInstanced`,
  `drawRangeElements`, `vertexAttribDivisor`. A divisor of 1 maps to SDL's
  per-instance vertex input rate; SDL 3.2 reserves larger step rates, so a
  divisor above 1 is clamped to 1.
- **Integer attributes and uniforms**: `vertexAttribIPointer`, the
  `vertexAttribI4*` family, `uniform1ui`…`uniform4ui`.
- **Sampler objects**: `createSampler`, `bindSampler`, `deleteSampler`,
  `samplerParameteri/f`, `getSamplerParameter`; a bound sampler overrides the
  texture's own filter/wrap state for that unit.
- **Immutable textures and buffer copies**: `texStorage2D` (the backend always
  allocates the full mip chain), `copyBufferSubData`, `getBufferSubData`.
- **Drawing buffer**: `drawingBufferStorage(width, height)` fixes the drawing
  buffer size; `canvas.width`/`canvas.height` no longer rebuild it afterwards.
- **Clears**: `clearBufferfv`, `clearBufferiv`, `clearBufferuiv`,
  `clearBufferfi`.
- **Multiple render targets**: `drawBuffers` addresses up to four colour
  attachments and `readBuffer` selects the attachment `readPixels` reads;
  `framebufferTexture2D` accepts `COLOR_ATTACHMENT0`…`3`, and a fragment
  shader's multiple `out` variables are written in order.
- **Queries and fences**: the objects and entry points exist
  (`createQuery`/`beginQuery`/`endQuery`/`getQueryParameter`,
  `fenceSync`/`clientWaitSync`/`waitSync`/`getSyncParameter`) and degrade
  gracefully: a fence reports `ALREADY_SIGNALED` and query results are 0.
- **Uniform blocks**: `getUniformBlockIndex`,
  `getActiveUniformBlockParameter`, `getActiveUniformBlockName`,
  `uniformBlockBinding`, `bindBufferBase`/`bindBufferRange` and
  `getIndexedParameter`. Members are merged into the engine's single std140
  block and copied from the bound buffer at draw time; scalars, vectors,
  matrices and fixed-size arrays work, nested structs, sampler members and
  blocks without an instance name are rejected.
- **Transform feedback**: the entry points are present as no-ops so
  feature-probing libraries do not throw.

Textures are created with a full mip chain; sampling clamps to level 0 until
`generateMipmap` runs. Matrix vertex attributes consume the right number of
consecutive locations and read one column per location. A disabled attribute
is supplied from the last `vertexAttrib*f` value through a small constant
buffer. A vertex shader can sample textures just like a fragment shader:
`MAX_VERTEX_TEXTURE_IMAGE_UNITS` reports 16, vertex samplers live in set 0 and
are bound with `SDL_BindGPUVertexSamplers` on every draw. Depth values use the
backend's `[0, 1]` clip space rather than GL's `[-1, 1] → [0, 1]` remap:
`gl_Position.z = 0.5` stores depth `0.5` (real WebGL stores `0.75`).

## Not implemented

Transform feedback (the entry points are no-ops: SDL_GPU has no transform
feedback stage) and MSAA/sample buffers (SDL_GPU 3.2 can resolve multisampled
colour targets, but its Metal resolve returns undefined pixels with scissor
tests and instanced draws, so contexts stay single-sampled and `SAMPLE_BUFFERS`
reports 0). `blitFramebuffer` copies colour and depth; stencil bits are accepted
but not copied because SDL_GPU cannot sample a stencil attachment.

Shader limitations that remain: struct-typed uniforms and user uniform blocks
are flattened, but built-in interface blocks (`gl_PerVertex` redeclarations) are
not part of the translated subset. A shader that uses one still fails with a
compile/link log rather than silently misbehaving.

## Platform

The pipeline is created with the shader format the SDL_GPU device accepts:
**SPIR-V** (`SDL_GPU_SHADERFORMAT_SPIRV`) on Vulkan, which is what Windows and
Linux select, or **MSL** (`SDL_GPU_SHADERFORMAT_MSL`) on Metal, which macOS
selects. Both backends go through the same GLSL front end, so a shader that
compiles on one compiles on the other. On a device whose backend accepts neither
format — Direct3D 12, which would need DXIL — `getContext("webgl")` and
`getContext("webgl2")` both answer `null`; the 2D canvas and the rest of the page
still work.

glslang and SPIRV-Cross are fetched and built by `npm run gui` and merged into
`gui.a` / `gui.lib` (see [Packaging and build](./build.md)); a program that only
uses the 2D canvas or the DOM pays no run-time cost beyond the archive size.

## Testing

- `tests/e2e/gui-webgl.test.ts` compiles GLSL, draws a textured and a
  depth-tested quad, checks face culling and reads the pixels back; it also
  covers blend constants, dynamic buffer usage, vertex-shader texture sampling,
  `gl_FragDepth`, context loss/restore and cube maps.
- `tests/e2e/gui-webgl2-shadow-sampler.test.ts` covers hardware
  `sampler2DShadow` comparisons, a shader-written `gl_FragDepth` that occludes a
  later draw, and a partial `blitFramebuffer` depth copy.
- `tests/e2e/gui-webgl2.test.ts` covers the WebGL 2 shader/state additions:
  `#version 300 es` shaders, a VAO round trip, instanced draws with a divisor,
  `texStorage2D`, `clearBufferfv`, buffer copies, sampler objects, struct-uniform
  fields and `drawingBufferStorage`.
- `tests/e2e/gui-webgl2-formats.test.ts` covers stencil, float/depth textures,
  compressed textures and `ImageBitmap`.
- `tests/e2e/gui-webgl2-blit.test.ts` covers scissored colour/depth clears,
  framebuffer blits, and the pixels around a partial blit that must survive
  (a destination load-op that discards them fails there on Metal).
- `tests/e2e/gui-media.test.ts` also uploads the latest decoded `<video>` frame
  as a texture and reads it back.
- `tests/e2e/gui-webgl2-3d.test.ts` covers 2D-array and 3D texture layer
  sampling, `texSubImage3D` sub-volume updates and rendering to a colour
  attachment layer.
- `tests/e2e/gui-webgl2-mrt.test.ts` writes two fragment outputs to two
  colour attachments and reads each back.
- `tests/e2e/gui-webgl2-ubo.test.ts` binds a std140 block, updates its members
  and verifies the drawn colour.
- `examples/gui/webgl-cube` is a spinning, lit, textured cube; it runs in a
  browser unchanged and is covered by
  `tests/e2e/gui-example-webgl.test.ts`.
- `examples/gui/webgl2-cubes` draws 64 instanced cubes with one draw call,
  `#version 300 es` and a VAO; covered by
  `tests/e2e/gui-example-webgl2.test.ts`.

These suites run against whichever backend the host selects: Vulkan on Linux
(and Windows), Metal on macOS, so the SPIRV-Cross -> MSL path is covered by the
same assertions as the SPIR-V one.

## three.js

The shader rewriter expands three.js's `#pragma unroll_loop_start` loops into
constant indices (its sampler arrays would otherwise fail Vulkan GLSL), and the
`.js` asset loader keeps the renderer's own unroller out of the way.
`WebGLRenderer` runs unchanged on this engine's real WebGL 2 backend, so **no
Canvas2D fallback is bundled any more**. Verified with three.js r160 through SDL_GPU:
`MeshBasicMaterial`, `MeshStandardMaterial` and `MeshPhongMaterial`,
`CanvasTexture`, linear fog, ambient/directional/point/spot/hemisphere lights,
`Sprite`, `Line`, `Points` and `InstancedMesh`.

Features outside this engine's subset still cannot be emulated: transform
feedback and MSAA. Depth textures, float render targets and compressed textures
are available, and comparative `sampler2DShadow` sampling is translated, so
three.js shadow maps can use the hardware depth compare on both backends. A
scene that needs transform feedback or MSAA will not render correctly;
everything the previous Canvas2D fallback could show now goes through the GPU.