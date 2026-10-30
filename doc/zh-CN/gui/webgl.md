# WebGL（由 GPU 绘制的 `<canvas>`）

> 语言 / Language：[English](../../gui/webgl.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md)；引擎总览、已定决策与路线图都在那里。

`<canvas>` 可以申请 WebGL 上下文，而不是 2D 上下文：

```ts
const canvas: any = document.getElementById("scene");
const gl: any = canvas.getContext("webgl2"); // 或 "webgl" / "experimental-webgl"
if (gl == null) {
  // 该平台的 GPU 后端无法消费 WebGL 的着色器
}
```

2D 上下文把图形光栅化到 CPU 位图再上传为纹理；WebGL 上下文则**在 GPU 上渲染**：顶点变换、光照、纹理采样、深度测试与混合全部在 SDL_GPU 管线中执行（Windows/Linux 上是 Vulkan，macOS 上是 Metal）。页面直接合成该上下文的颜色纹理，因此显示时不需要把像素读回 CPU。

同时提供 **WebGL 1.0** 与 **WebGL 2.0**。一个 canvas 会固定为它第一次申请的上下文类型：先调用 `getContext("webgl2")` 之后，`getContext("webgl")` 返回 `null`，反之亦然。WebGL 2 上下文拥有自己的 `WebGL2RenderingContext` 原型（其中复制了 WebGL 1 的常量与方法），因此 `gl instanceof WebGL2RenderingContext` 为真，而 `gl instanceof WebGLRenderingContext` 为假，与浏览器一致——three.js 等库正是据此选择 WebGL 2 路径。`gl.constructor.name` 也会报告对应类型名。

## 一帧如何到达屏幕

- 每个上下文拥有一张离屏 `R8G8B8A8_UNORM` 颜色纹理（绘图缓冲）和一张 `D32_FLOAT` 深度纹理，尺寸取自 canvas 的 `width` / `height` 属性。`canvas.width = …` 会重建它们并丢弃内容，与 DOM 规则一致。
- 上下文记录到自己的 SDL_GPU 命令缓冲。`clear`/`draw*` 保持一个渲染 pass 打开；只有缓冲区/纹理上传或切换 framebuffer 才会结束它，并以 `LOAD` 开始新 pass，因此绘图缓冲在多次绘制之间保留，和 WebGL 一样；没有上传的绘制不会中断 pass。
- 每帧一次，在页面自己的渲染 pass 之前，引擎提交所有 WebGL 命令缓冲。随后图像管线把颜色纹理当作普通 `PaintImage` 采样，没有 CPU 拷贝。
- WebGL 原点在左下，SDL_GPU 渲染目标原点在左上，因此帧缓冲按自上而下存储：转换 `viewport`/`scissor`，`readPixels` 再把行翻回去，合成器看到的是正立的纹理。
- 固定功能状态变化（`blendFunc`、`depthFunc`、`cullFace`、`colorMask` 等）会改变所需的 `SDL_GPUGraphicsPipeline`，因此管线惰性创建并缓存在程序上。键是这次绘制的**内容**：程序、图元模式、顶点布局的哈希（属性位置、格式、偏移、缓冲 pitch 与输入速率）、反射出的固定功能状态、附件格式。它刻意**不**包含上下文的单调版本计数器（`attribs_version_`、`framebuffer_version_`）与 GPU 缓冲句柄：这些值在每次绑定 VAO、切换帧缓冲或重建缓冲时都会前进，却不改变管线；用它们做键会让每帧绑定多个布局的场景（three.js 每帧为每个几何体绑定一个 VAO）每帧重建全部管线 —— 实测每帧 248 次 `SDL_CreateGPUGraphicsPipeline`，且程序级缓存无界增长。`tests/e2e/gui-webgl-pipeline-cache.test.ts` 固定了稳态：布局相同、缓冲不同的两次绘制在首帧之后不再创建管线。

## 性能

重复绘制命中缓存，而不是每次重建：当前（程序、属性布局、固定功能状态、附件格式）
对应的管线、顶点布局与缓冲绑定只准备一次并复用（`webgl_draw_setup.cpp`）；状态变化时
缓存未命中并新建管线。固定功能状态被哈希进 128 位键，而不是每次 draw 拼字符串；活动
属性在链接时解析一次。上传被前移到渲染 pass 之前：没有上传的绘制不会结束 pass，禁用
的顶点属性读取持久的常量缓冲而不是每次 draw 分配 GPU 缓冲，状态未变的绘制不重复绑定。
`bufferSubData` 只拷贝写入的字节区间，增长中的缓冲按 2 的幂扩容。

`win.stats()` 报告 `webglDraws`、`webglPasses`、`webglPipelineMisses` 与
`webglBufferUploadBytes`；`tests/e2e/gui-draw-perf.test.ts` 正是用它们固定行为
（2,000 次绘制只开一个 pass、稳定状态不再创建管线、4 MiB 缓冲只重传 64 KiB 区间）。
剩余的每次 draw 开销来自后端自身的提交；大量相同对象仍应由应用改用
`drawElementsInstanced`。

## 着色器：GLSL ES 到 SPIR-V（再到 MSL）

SDL_GPU 不能在运行时编译 GLSL，且每个后端只消费自己的格式，因此引擎静态链接了两个翻译器：

- **glslang** 把改写后的 GLSL 编译为 SPIR-V，Vulkan 后端直接消费；
- **SPIRV-Cross** 把同一份 SPIR-V 交叉编译为 Metal Shading Language 文本，Metal 后端在运行时编译它。

两者都打进 `gui.a` / `gui.lib`；Metal 路径复用 SPIR-V 模块而不是再写一套 GLSL 前端，因此两个后端接受完全相同的着色器子集。流程是：

1. `shaderSource` 保存 GLSL ES 文本；`compileShader` 先翻译一次，供 `COMPILE_STATUS` / `getShaderInfoLog` 使用。
2. `linkProgram` 用共享的属性/`varying` 位置表再翻译两个阶段，用 glslang 编译，然后创建 `SDL_GPUShader`：Vulkan 上直接用 SPIR-V，Metal 上用 SPIRV-Cross 产出的 MSL 文本。
3. 反射生成的 SPIR-V，取得 uniform 块精确的 std140 偏移、数组步长与矩阵步长——源码里数组长度可以写宏，glslang 已经解析好了。SPIRV-Cross 会在 MSL 结构体中保留这些偏移，因此一份 uniform 布局同时服务两个后端。

翻译器（`runtime/ext_gui/webgl_glsl*.cpp`）处理 GLSL ES 1.00 与 3.00：

| WebGL 写法 | 改写为 |
| --- | --- |
| `attribute vec3 a_pos` | `layout(location = N) in vec3 a_pos` |
| `varying vec2 v_uv` | `layout(location = N) out/in vec2 v_uv`（共享的名字 → 位置表） |
| `gl_FragColor` | `layout(location = 0) out highp vec4 xt_FragColor` |
| `texture2D`、`textureCube`、`texture2DProj` | `texture`、`texture`、`textureProj` |
| `texture2DLodEXT` / `textureCubeLodEXT` | `textureLod` |
| 非采样器 uniform | 一个 `layout(std140) uniform XtUniforms { … }` 块 |
| `uniform sampler2D u_tex` | `layout(set = 2, binding = N) uniform sampler2D u_tex`（片元）或 set 0（顶点） |
| `gl_FragData[0]` | `xt_FragColor` |
| `gl_FragDepth` / `gl_FragDepthEXT` | 保留（SPIR-V `DepthReplacing`；MSL `[[depth(any)]]`） |
| `sampler2DShadow` / `samplerCubeShadow` / `sampler2DArrayShadow` | 保留；绑定为 SDL_GPU 比较采样器 |
| 缺少 `#version` / `#version 100` | `#version 310 es` |

实现限制：结构体 uniform、用户 uniform 块、预处理阶段无法确定长度的 uniform 数组，以及用非常量表达式索引的采样器数组会被拒绝（shader/link info log 会说明）。`#pragma unroll_loop_start` 循环会在改写前展开，因此 three.js 生成的 `sampler2D shadowMap[ NUM_DIR_LIGHT_SHADOWS ]` / `shadowMap[ i ]` 会变成常量索引。`#ifdef` 内的着色器代码可用；非采样器 uniform 声明会被移动到顶层，保证块保持无条件。两个阶段的非采样器 uniform 会合并成同一个块，并在每个阶段各声明一次（顶点 set 1、片元 set 3），与 SDL_GPU 的 Vulkan 描述符布局一致。在 Metal 上同一批声明会变成 `[[buffer(0)]]` 与 `[[texture(N)]]`/`[[sampler(N)]]`，正是 SDL_GPU Metal 后端绑定的槽位。`uniformMatrix4fv` 按列主序写入反射出的偏移；对采样器调用 `uniform1i` 时记录纹理单元。每次绘制都用动态 uniform 缓冲接口推送该块。

翻译器的限制：结构体 uniform、用户 uniform 块、预处理后仍无法确定长度的 uniform 数组、采样器数组与采样器类型不匹配的声明会被拒绝（着色器/链接日志会说明）。`#ifdef` 内的代码没问题；非采样器 uniform 声明会被移到外层顶层位置，保证块是无条件的。

片元着色器可以写 `gl_FragDepth`：glslang 会把 SPIR-V 标记为 `DepthReplacing`，SPIRV-Cross 会生成 `[[depth(any)]]`，因此两个后端上深度写入都会到达深度附件。比较采样器（`sampler2DShadow`、`samplerCubeShadow`、`sampler2DArrayShadow`）复用 SDL_GPU 采样器对象并设置 `enable_compare`；`TEXTURE_COMPARE_MODE` 与 `TEXTURE_COMPARE_FUNC` 从纹理读取（WebGL 2 也可从绑定的采样器对象读取）。

### GLSL ES 3.00（WebGL 2）

`#version 300 es` 源码除版本号提升外会被原样接受：顶层 `in`/`out` 声明会变成属性/`varying`，命名片元输出（`out vec4 fragColor;`）会变成 `xt_FragColor` 形式的输出，`texture()` / `textureLod()` 本就与 Vulkan 方言一致。多个片元输出会分配连续 location，但渲染 pass 只有一个颜色目标，因此只写入第一个。GLSL ES 3.00 提供 `gl_VertexID` 与 `gl_InstanceID`，实例化示例正是用它。

## 支持的 API

- **上下文**：`getContext("webgl")`、`canvas`、`drawingBufferWidth/Height`、`getParameter`、`getError`、`getSupportedExtensions`、`getExtension`、`getContextAttributes`、`isContextLost`、`getShaderPrecisionFormat`，以及 8 位模板缓冲（`getContextAttributes().stencil` 为 `true`，`STENCIL_BITS` 为 8）。`WEBGL_lose_context` 的 `loseContext`/`restoreContext` 会真正丢弃并重建上下文（对象全部失效），并派发 `webglcontextlost` / `webglcontextrestored` 事件。
- **着色器与程序**：`createShader`、`shaderSource`、`compileShader`、`getShaderParameter`、`getShaderInfoLog`、`getShaderSource`、`deleteShader`、`createProgram`、`attachShader`、`detachShader`、`linkProgram`、`getProgramParameter`、`getProgramInfoLog`、`getAttachedShaders`、`validateProgram`、`deleteProgram`、`useProgram`、`getActiveUniform`/`getActiveAttrib`、`getUniformLocation`、`getAttribLocation`、`bindAttribLocation`。
- **Uniform**：`uniform1f`…`uniform4f`、`uniform1i`…`uniform4i`、`uniform1fv`…`uniform4fv`、`uniform1iv`…`uniform4iv`、`uniformMatrix2fv`/`3fv`/`4fv`、`getUniform`。
- **缓冲与顶点属性**：`createBuffer`、`bindBuffer`、`bufferData`、`bufferSubData`、`deleteBuffer`、`getBufferParameter`、`enableVertexAttribArray`、`disableVertexAttribArray`、`vertexAttribPointer`、`vertexAttrib1f`…`4f` / `1fv`…`4fv`、`getVertexAttrib`。`bufferData` 的 `usage` 提示（`STREAM_DRAW`/`STATIC_DRAW`/`DYNAMIC_DRAW`）会被记录，`getBufferParameter(…, BUFFER_USAGE)` 可读回。
- **绘制**：`drawArrays`、`drawElements`（UNSIGNED_BYTE、UNSIGNED_SHORT、UNSIGNED_INT；`TRIANGLE_FAN` 与 `LINE_LOOP` 会展开为列表）。`POINTS`、`LINES`、`LINE_STRIP`、`TRIANGLES`、`TRIANGLE_STRIP` 映射到对应的 SDL 图元。
- **纹理**：`createTexture`、`bindTexture`、`activeTexture`、`texParameteri`/`texParameterf`、`getTexParameter`、`texImage2D`（原始像素、`<img>`、`<canvas>`、`ImageData`）、`texSubImage2D`、`generateMipmap`、`deleteTexture`。支持 `UNPACK_FLIP_Y_WEBGL` 与 `UNPACK_PREMULTIPLY_ALPHA_WEBGL`。默认转换为 RGBA8；`RGBA`/`RGB`/`LUMINANCE`/`ALPHA`/`LUMINANCE_ALPHA` 配 `UNSIGNED_BYTE` 可用。浮点纹理（`RGBA32F`、`RGBA16F`、`RGB32F`、`RGB16F`、`RG32F`/`RG16F`、`R32F`/`R16F`，`FLOAT` 或 `HALF_FLOAT` 数据，以及 WebGL 1 的 `OES_texture_float`/`OES_texture_half_float`）映射到 SDL 的 RGBA32F/RGBA16F；深度纹理（`DEPTH_COMPONENT16/24/32F`、`DEPTH_STENCIL`、`DEPTH24_STENCIL8`）映射到 `D32_FLOAT`/`D32_FLOAT_S8_UINT`，可作为采样源或深度附件。压缩纹理通过 `compressedTexImage2D`/`compressedTexSubImage2D` 上传，支持 `WEBGL_compressed_texture_s3tc`（BC1/BC2/BC3）、`WEBGL_compressed_texture_s3tc_srgb` 与 `WEBGL_compressed_texture_astc`；扩展只在设备确实支持对应格式时公布。`texImage2D` 还接受 `<video>` 的最新解码帧与 `createImageBitmap` 产生的 `ImageBitmap`。立方体贴图（`bindTexture(TEXTURE_CUBE_MAP)` + 六个 `texImage2D` 面）与 3D/数组纹理（`texImage3D`/`texStorage3D`）可用对应采样器（`samplerCube`、`sampler3D`、`sampler2DArray`）采样；`texSubImage3D` 可更新 3D/数组纹理的子区域，`framebufferTextureLayer` 可把数组/立方体贴图的指定层作为颜色附件渲染。
- **Framebuffer**：`createFramebuffer`、`bindFramebuffer`（`FRAMEBUFFER`/`READ_FRAMEBUFFER`/`DRAW_FRAMEBUFFER`）、`framebufferTexture2D`（`COLOR_ATTACHMENT0`、`DEPTH_ATTACHMENT`、`DEPTH_STENCIL_ATTACHMENT`）、`checkFramebufferStatus`、`createRenderbuffer`、`bindRenderbuffer`、`renderbufferStorage`（深度/深度模板）、`framebufferRenderbuffer`、`delete*`，以及 `blitFramebuffer`（颜色走 SDL blit；深度走一个写 `gl_FragDepth` 的采样 pass；模板位被接受但不复制）。足以实现渲染到纹理、阴影贴图与后处理。
- **状态**：`enable`/`disable`/`isEnabled`（`BLEND`、`DEPTH_TEST`、`CULL_FACE`、`SCISSOR_TEST`、`STENCIL_TEST`）、`clearColor`/`clearDepth`/`clearStencil`、`clear`（开启 `SCISSOR_TEST` 时颜色、深度与模板都只清除 scissor 矩形）、`blendColor`、`blendFunc`/`blendFuncSeparate`、`blendEquation`/`blendEquationSeparate`、`depthFunc`、`depthMask`、`colorMask`、`stencilFunc`/`stencilFuncSeparate`、`stencilOp`/`stencilOpSeparate`、`stencilMask`/`stencilMaskSeparate`、`cullFace`、`frontFace`、`viewport`、`scissor`、`pixelStorei`、`finish`。模板测试真正参与光栅化，`clear(STENCIL_BUFFER_BIT)` 会清空模板附件；SDL_GPU 只有一份前后脸共用的 reference 与 compare/write mask，若前后脸设置不同，使用前脸值。
- **读回**：`readPixels`（RGBA/UNSIGNED_BYTE），可从默认帧缓冲或绑定的 RGBA8 framebuffer 读取；浮点/深度附件先采样到 RGBA8 目标再读回。它会阻塞到 GPU 完成；用于测试和截图，不要每帧调用。

WebGL 2 额外提供：

- **顶点数组对象**：`createVertexArray`、`bindVertexArray`、`deleteVertexArray`、`isVertexArray`。SDL_GPU 没有 VAO，绑定一个会恢复保存下来的属性表及其元素缓冲。
- **实例化**：`drawArraysInstanced`、`drawElementsInstanced`、`drawRangeElements`、`vertexAttribDivisor`。divisor 为 1 映射到 SDL 的按实例输入频率；SDL 3.2 保留更大的步进值，因此大于 1 的 divisor 会被钳制为 1。
- **整数属性与 uniform**：`vertexAttribIPointer`、`vertexAttribI4*` 系列、`uniform1ui`…`uniform4ui`。
- **采样器对象**：`createSampler`、`bindSampler`、`deleteSampler`、`samplerParameteri/f`、`getSamplerParameter`；绑定的采样器会覆盖该单元上纹理自身的过滤/环绕状态。
- **不可变纹理与缓冲拷贝**：`texStorage2D`（后端始终分配完整 mip 链）、`copyBufferSubData`、`getBufferSubData`。
- **绘图缓冲**：`drawingBufferStorage(width, height)` 固定绘图缓冲尺寸；此后 `canvas.width` / `canvas.height` 不再重建它。
- **清除**：`clearBufferfv`、`clearBufferiv`、`clearBufferuiv`、`clearBufferfi`。
- **多渲染目标**：`drawBuffers` 最多 4 个颜色附件，`readBuffer` 选择 `readPixels` 的附件；`framebufferTexture2D` 可挂 `COLOR_ATTACHMENT0`…`3`，片元着色器的多个 `out` 按顺序写入。
- **查询与栅栏**：对象与入口都存在（`createQuery`/`beginQuery`/`endQuery`/`getQueryParameter`、`fenceSync`/`clientWaitSync`/`waitSync`/`getSyncParameter`），行为是优雅降级：栅栏报告 `ALREADY_SIGNALED`，查询结果为 0。
- **Uniform 块**：`getUniformBlockIndex`、`getActiveUniformBlockParameter`、`getActiveUniformBlockName`、`uniformBlockBinding`、`bindBufferBase`/`bindBufferRange`、`getIndexedParameter`。块成员按 std140 合并进引擎的单一 uniform 块，绘制时从绑定缓冲复制；支持标量/向量/矩阵与定长数组，不支持嵌套结构体、采样器成员和省略实例名的块。
- **Transform feedback**：入口存在但为空操作，避免特性探测类库抛错。

纹理创建时带完整 mip 链；在 `generateMipmap` 之前采样被钳制到第 0 层。矩阵顶点属性会占用相应数量的连续 location，每个 location 读取一列。未启用的属性由最近一次 `vertexAttrib*f` 的值通过一个小常量缓冲提供。顶点着色器可以像片元着色器一样采样纹理：`MAX_VERTEX_TEXTURE_IMAGE_UNITS` 报告 16，顶点采样器走 set 0，并在每次绘制时通过 `SDL_BindGPUVertexSamplers` 绑定。深度值按后端的 `[0, 1]` 裁剪空间存储，不做 GL 的 `[-1, 1] → [0, 1]` 重映射：`gl_Position.z = 0.5` 写入的深度是 `0.5`（真实 WebGL 为 `0.75`）。

## 未实现

transform feedback（入口为空操作：SDL_GPU 没有 transform feedback 阶段）与 MSAA/采样缓冲（SDL_GPU 3.2 能解析多采样颜色目标，但其 Metal resolve 在 scissor 与实例化绘制下会返回未定义像素，因此上下文保持单采样，`SAMPLE_BUFFERS` 报告 0）。`blitFramebuffer` 复制颜色与深度；模板位被接受但不复制，因为 SDL_GPU 无法采样模板附件。

仍存在的着色器限制：结构体 uniform 与用户 uniform 块会被展平，但内建接口块（如重声明 `gl_PerVertex`）不在翻译子集内；使用它的着色器仍会以编译/链接日志失败，而不是静默出错。

## 平台

管线按 SDL_GPU 设备接受的着色器格式创建：Vulkan 上用 **SPIR-V**（`SDL_GPU_SHADERFORMAT_SPIRV`，Windows/Linux 选择的后端），macOS 上用 **MSL**（`SDL_GPU_SHADERFORMAT_MSL`，Metal 后端）。两个后端共用同一个 GLSL 前端，因此在一个后端能编译的着色器在另一个上也能编译。如果设备的后端两种格式都不接受——Direct3D 12 需要 DXIL——`getContext("webgl")` 与 `getContext("webgl2")` 都会返回 `null`；2D canvas 与页面其余部分仍然正常。

`npm run gui` 会拉取并构建 glslang 并合并进 `gui.a` / `gui.lib`；SPIRV-Cross **只在 macOS 上**合并，因为其他 SDL_GPU 后端都不消费 MSL（见[打包与构建](./build.md)）。两者都通过可选的 **`gpu` 链接特性**访问：编译器在程序可能调用 `getContext("webgl…")` 或使用 `mix-blend-mode` 时选中它，只有那时 bridge 才会把翻译器成员从 `gui.a` 里取出。只用 2D canvas 或 DOM 的程序既不链接 glslang 也不链接 SPIRV-Cross（见[链接特性](./build.md#链接特性)）。

## 测试

- `tests/e2e/gui-webgl.test.ts` 编译 GLSL、绘制带纹理和深度测试的四边形、检查面剔除，并读回像素；它还覆盖混合常量色、动态缓冲 usage、顶点着色器纹理采样、`gl_FragDepth`、上下文丢失/恢复与立方体贴图。
- `tests/e2e/gui-webgl2-shadow-sampler.test.ts` 覆盖硬件 `sampler2DShadow` 比较、着色器写 `gl_FragDepth` 遮挡后续绘制，以及 `blitFramebuffer` 的部分深度复制。
- `tests/e2e/gui-webgl2.test.ts` 覆盖 WebGL 2 的着色器/状态新增能力：`#version 300 es` 着色器、VAO 往返、带 divisor 的实例化绘制、`texStorage2D`、`clearBufferfv`、缓冲拷贝、采样器对象、结构体 uniform 字段与 `drawingBufferStorage`。
- `tests/e2e/gui-webgl2-formats.test.ts` 覆盖模板、浮点/深度纹理、压缩纹理与 `ImageBitmap`。
- `tests/e2e/gui-webgl2-blit.test.ts` 覆盖带 scissor 的颜色/深度清除、framebuffer blit，以及局部 blit 之外必须保留的像素（在 Metal 上，若目标 load-op 丢弃这些像素，该测试就会失败）。
- `tests/e2e/gui-webgl2-3d.test.ts` 覆盖 2D 数组纹理与 3D 纹理的多层采样、`texSubImage3D` 子区域更新与 `framebufferTextureLayer` 渲染到数组层。
- `tests/e2e/gui-webgl2-mrt.test.ts` 覆盖两个片元输出分别写入两个颜色附件。
- `tests/e2e/gui-webgl2-ubo.test.ts` 覆盖 uniform 块的绑定、std140 成员与运行时更新。
- `tests/e2e/gui-media.test.ts` 还会把 `<video>` 的最新解码帧上传为纹理并读回像素。
- `examples/gui/webgl-cube` 是一个旋转、受光、带纹理的立方体；它在浏览器中原样运行，并由 `tests/e2e/gui-example-webgl.test.ts` 覆盖。
- `examples/gui/webgl2-cubes` 用一次绘制调用画出 64 个实例化立方体，使用 `#version 300 es` 与 VAO；由 `tests/e2e/gui-example-webgl2.test.ts` 覆盖。

这些用例会在宿主选择的后端上运行：Linux（与 Windows）上是 Vulkan，macOS 上是 Metal，因此 SPIRV-Cross → MSL 路径与 SPIR-V 路径受同一组断言保护。

## three.js

着色器改写器会把 three.js 的 `#pragma unroll_loop_start` 循环展开为常量索引（否则其采样器数组无法通过 Vulkan GLSL），`.js` 资源加载器则避免渲染器自带的展开器重复处理。`WebGLRenderer` 在本引擎真正的 WebGL 2 后端上原样运行，**不再打包 Canvas2D 回退**。已用 three.js r160 通过 SDL_GPU 验证：`MeshBasicMaterial`、`MeshStandardMaterial`、`MeshPhongMaterial`、`CanvasTexture`、线性雾、环境光/平行光/点光/聚光/半球光、`Sprite`、`Line`、`Points` 与 `InstancedMesh`。

本引擎子集之外的能力仍无法模拟：transform feedback 与 MSAA。立方体贴图/环境贴图、深度纹理、HDR/浮点渲染目标、压缩纹理与比较型 `sampler2DShadow` 采样都已可用（three.js 阴影贴图可在两个后端使用硬件深度比较）。需要 transform feedback 或 MSAA 的场景无法正确渲染；此前 Canvas2D 回退能显示的内容现在全部走 GPU。