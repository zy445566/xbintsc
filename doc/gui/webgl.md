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
(Vulkan) pipeline. The page composites the context's colour texture directly, so
no pixels are read back to the CPU for display.

Both **WebGL 1.0** and **WebGL 2.0** are available. A canvas keeps the first
context kind it asks for: after `getContext("webgl2")` a later
`getContext("webgl")` answers `null`, and the other way round. A WebGL 2 context
also satisfies `instanceof WebGLRenderingContext` (its prototype inherits the
WebGL 1 one) and adds the WebGL 2 constants and methods.

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

## Shaders: GLSL ES to SPIR-V

SDL_GPU cannot compile GLSL at run time and its Vulkan backend consumes SPIR-V,
so the engine ships **glslang** (statically linked into `gui.a`). The path is:

1. `shaderSource` stores the GLSL ES text; `compileShader` translates it once
   for `COMPILE_STATUS` / `getShaderInfoLog`.
2. `linkProgram` translates both stages again with one shared attribute and
   varying location map, compiles them with glslang, and creates the
   `SDL_GPUShader` objects.
3. The generated SPIR-V is reflected to obtain the exact std140 offsets, array
   strides and matrix strides of the uniform block — the shader text may size an
   array with a macro, but glslang has already resolved it.

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
| a missing `#version` / `#version 100` | `#version 310 es` |

Non-sampler uniforms are merged across both stages and emitted as one block in
each stage (set 1 for the vertex shader, set 3 for the fragment shader), which
matches SDL_GPU's Vulkan descriptor layout. `uniformMatrix4fv` writes
column-major floats into the reflected offsets; `uniform1i` on a sampler stores
the texture unit. The block is pushed with the dynamic uniform-buffer API on
every draw.

Limits of the translator: struct-typed uniforms, user uniform blocks, uniform
arrays whose size is not resolved by the preprocessor, sampler arrays and cube
maps are rejected (the link/shader info log says so). Shader code inside
`#ifdef` is fine; non-sampler uniform declarations are moved to the enclosing
top-level position so the block stays unconditional.

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
  `getContextAttributes`, `isContextLost`, `getShaderPrecisionFormat`, and the
  stencil entry points (accepted; the depth target has no stencil attachment).
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
  `getVertexAttrib`.
- **Drawing**: `drawArrays`, `drawElements` (UNSIGNED_BYTE, UNSIGNED_SHORT,
  UNSIGNED_INT; `TRIANGLE_FAN` and `LINE_LOOP` are expanded to lists).
  `POINTS`, `LINES`, `LINE_STRIP`, `TRIANGLES` and `TRIANGLE_STRIP` map to the
  matching SDL primitive.
- **Textures**: `createTexture`, `bindTexture`, `activeTexture`,
  `texParameteri`/`texParameterf`, `getTexParameter`, `texImage2D` (raw pixels,
  `<img>`, `<canvas>` and `ImageData` sources), `texSubImage2D`,
  `generateMipmap`, `deleteTexture`. `UNPACK_FLIP_Y_WEBGL` and
  `UNPACK_PREMULTIPLY_ALPHA_WEBGL` are honoured. Everything is converted to
  RGBA8; `RGBA`, `RGB`, `LUMINANCE`, `ALPHA` and `LUMINANCE_ALPHA` with
  `UNSIGNED_BYTE` are accepted.
- **Framebuffers**: `createFramebuffer`, `bindFramebuffer`,
  `framebufferTexture2D` (colour attachment), `checkFramebufferStatus`,
  `createRenderbuffer`, `bindRenderbuffer`, `renderbufferStorage` (depth),
  `framebufferRenderbuffer`, `delete*`. This is enough for render-to-texture,
  shadow maps and post-processing passes.
- **State**: `enable`/`disable`/`isEnabled` for `BLEND`, `DEPTH_TEST`,
  `CULL_FACE`, `SCISSOR_TEST` (stencil accepted but not applied),
  `clearColor`/`clearDepth`/`clearStencil`, `clear`, `blendFunc`/
  `blendFuncSeparate`, `blendEquation`/`blendEquationSeparate`, `depthFunc`,
  `depthMask`, `colorMask`, `cullFace`, `frontFace`, `viewport`, `scissor`,
  `pixelStorei`, `finish`.
- **Readback**: `readPixels` (RGBA/UNSIGNED_BYTE) from the default framebuffer
  or a bound framebuffer. It blocks until the GPU finishes; use it for tests and
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
- **Clears**: `clearBufferfv`, `clearBufferiv`, `clearBufferuiv`,
  `clearBufferfi`.
- **Queries and fences**: the objects and entry points exist
  (`createQuery`/`beginQuery`/`endQuery`/`getQueryParameter`,
  `fenceSync`/`clientWaitSync`/`waitSync`/`getSyncParameter`) and degrade
  gracefully: a fence reports `ALREADY_SIGNALED` and query results are 0.
- **Uniform blocks**: `getUniformBlockIndex` answers `INVALID_INDEX` so a
  library falls back to individual uniforms; the block entry points are no-ops.
- **Transform feedback, 3D textures and compressed formats**: the entry points
  are present as no-ops so feature-probing libraries do not throw.

Textures are created with a full mip chain; sampling clamps to level 0 until
`generateMipmap` runs. Matrix vertex attributes consume the right number of
consecutive locations and read one column per location. A disabled attribute
is supplied from the last `vertexAttrib*f` value through a small constant
buffer.

## Not implemented

Uniform buffer objects in shader source, transform feedback (the entry points
are no-ops), cube maps, 3D/array textures, float/half textures, depth textures,
compressed textures, multiple render targets (only the first fragment output is
written), `blitFramebuffer`, MSAA/sample buffers, stencil operations (state is
accepted, the stencil buffer is not written), constant blend colours,
`drawingBufferStorage`, context loss, `bufferData` with a `usage` other than
static/dynamic (the hint is ignored), `texImage2D` for
`HTMLVideoElement`/`ImageBitmap` and scissored `clear` (a clear clears the whole
attachment). Shaders that use built-in blocks, user uniform blocks or
`gl_FragDepth` beyond the supported subset are rejected with a compile/link log.

The engine advertises zero vertex texture units, so a well-behaved WebGL 1
program will not try to sample a texture in a vertex shader.

## Platform

The runtime shader compiler targets **Vulkan SPIR-V**, and the pipeline is
created with `SDL_GPU_SHADERFORMAT_SPIRV`. On a device whose SDL_GPU backend is
Metal or Direct3D 12 (and does not accept SPIR-V), `getContext("webgl")` and
`getContext("webgl2")` both answer `null`; the 2D canvas and the rest of the page
still work. Windows selects Vulkan (`win.driver()` reports it), so that is the
validated path.

glslang is fetched and built by `npm run gui` and merged into `gui.a` / `gui.lib`
(see [Packaging and build](./build.md)); a program that only uses the 2D canvas
or the DOM pays no run-time cost beyond the archive size.

## Testing

- `tests/e2e/gui-webgl.test.ts` compiles GLSL, draws a textured and a
  depth-tested quad, checks face culling and reads the pixels back.
- `tests/e2e/gui-webgl2.test.ts` covers the WebGL 2 additions: `#version 300 es`
  shaders, a VAO round trip, instanced draws with a divisor, `texStorage2D`,
  `clearBufferfv`, buffer copies and sampler objects.
- `examples/gui/webgl-cube` is a spinning, lit, textured cube; it runs in a
  browser unchanged and is covered by
  `tests/e2e/gui-example-webgl.test.ts`.
- `examples/gui/webgl2-cubes` draws 64 instanced cubes with one draw call,
  `#version 300 es` and a VAO; covered by
  `tests/e2e/gui-example-webgl2.test.ts`.

## three.js

The `.js` asset loader still recognises three.js, but its exported
`WebGLRenderer` now probes for a WebGL context first: with this backend the page
gets three.js's own renderer, and only a device without a WebGL context falls
back to the bundled Canvas2D scene renderer. The probe asks for `webgl2` first,
so recent three.js releases (which require WebGL 2) take their WebGL 2 path;
the parts of that path this engine does not implement yet — depth textures,
uniform blocks, transform feedback — can still make a particular scene fail, and
those pages should keep the Canvas2D fallback.
