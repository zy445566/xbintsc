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

同时提供 **WebGL 1.0** 与 **WebGL 2.0**。一个 canvas 会固定为它第一次申请的上下文类型：先调用 `getContext("webgl2")` 之后，`getContext("webgl")` 返回 `null`，反之亦然。WebGL 2 上下文也满足 `instanceof WebGLRenderingContext`（其原型继承 WebGL 1 原型），并在其上增加 WebGL 2 的常量与方法。

## 一帧如何到达屏幕

- 每个上下文拥有一张离屏 `R8G8B8A8_UNORM` 颜色纹理（绘图缓冲）和一张 `D32_FLOAT` 深度纹理，尺寸取自 canvas 的 `width` / `height` 属性。`canvas.width = …` 会重建它们并丢弃内容，与 DOM 规则一致。
- 上下文记录到自己的 SDL_GPU 命令缓冲。`clear`/`draw*` 保持一个渲染 pass 打开；缓冲区/纹理上传或切换 framebuffer 会结束它，并以 `LOAD` 开始新 pass，因此绘图缓冲在多次绘制之间保留，和 WebGL 一样。
- 每帧一次，在页面自己的渲染 pass 之前，引擎提交所有 WebGL 命令缓冲。随后图像管线把颜色纹理当作普通 `PaintImage` 采样，没有 CPU 拷贝。
- WebGL 原点在左下，SDL_GPU 渲染目标原点在左上，因此帧缓冲按自上而下存储：转换 `viewport`/`scissor`，`readPixels` 再把行翻回去，合成器看到的是正立的纹理。
- 固定功能状态变化（`blendFunc`、`depthFunc`、`cullFace`、`colorMask` 等）会改变所需的 `SDL_GPUGraphicsPipeline`，因此管线按（程序、顶点布局、状态）惰性创建，并缓存在程序上。

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
| 缺少 `#version` / `#version 100` | `#version 310 es` |

两个阶段的非采样器 uniform 会合并成同一个块，并在每个阶段各声明一次（顶点 set 1、片元 set 3），与 SDL_GPU 的 Vulkan 描述符布局一致。在 Metal 上同一批声明会变成 `[[buffer(0)]]` 与 `[[texture(N)]]`/`[[sampler(N)]]`，正是 SDL_GPU Metal 后端绑定的槽位。`uniformMatrix4fv` 按列主序写入反射出的偏移；对采样器调用 `uniform1i` 时记录纹理单元。每次绘制都用动态 uniform 缓冲接口推送该块。

翻译器的限制：结构体 uniform、用户 uniform 块、预处理后仍无法确定长度的 uniform 数组、采样器数组与立方体贴图都会被拒绝（着色器/链接日志会说明）。`#ifdef` 内的代码没问题；非采样器 uniform 声明会被移到外层顶层位置，保证块是无条件的。

### GLSL ES 3.00（WebGL 2）

`#version 300 es` 源码除版本号提升外会被原样接受：顶层 `in`/`out` 声明会变成属性/`varying`，命名片元输出（`out vec4 fragColor;`）会变成 `xt_FragColor` 形式的输出，`texture()` / `textureLod()` 本就与 Vulkan 方言一致。多个片元输出会分配连续 location，但渲染 pass 只有一个颜色目标，因此只写入第一个。GLSL ES 3.00 提供 `gl_VertexID` 与 `gl_InstanceID`，实例化示例正是用它。

## 支持的 API

- **上下文**：`getContext("webgl")`、`canvas`、`drawingBufferWidth/Height`、`getParameter`、`getError`、`getSupportedExtensions`、`getExtension`、`getContextAttributes`、`isContextLost`、`getShaderPrecisionFormat`，以及 8 位模板缓冲（`getContextAttributes().stencil` 为 `true`，`STENCIL_BITS` 为 8）。`WEBGL_lose_context` 的 `loseContext`/`restoreContext` 会真正丢弃并重建上下文（对象全部失效），并派发 `webglcontextlost` / `webglcontextrestored` 事件。
- **着色器与程序**：`createShader`、`shaderSource`、`compileShader`、`getShaderParameter`、`getShaderInfoLog`、`getShaderSource`、`deleteShader`、`createProgram`、`attachShader`、`detachShader`、`linkProgram`、`getProgramParameter`、`getProgramInfoLog`、`getAttachedShaders`、`validateProgram`、`deleteProgram`、`useProgram`、`getActiveUniform`/`getActiveAttrib`、`getUniformLocation`、`getAttribLocation`、`bindAttribLocation`。
- **Uniform**：`uniform1f`…`uniform4f`、`uniform1i`…`uniform4i`、`uniform1fv`…`uniform4fv`、`uniform1iv`…`uniform4iv`、`uniformMatrix2fv`/`3fv`/`4fv`、`getUniform`。
- **缓冲与顶点属性**：`createBuffer`、`bindBuffer`、`bufferData`、`bufferSubData`、`deleteBuffer`、`getBufferParameter`、`enableVertexAttribArray`、`disableVertexAttribArray`、`vertexAttribPointer`、`vertexAttrib1f`…`4f` / `1fv`…`4fv`、`getVertexAttrib`。`bufferData` 的 `usage` 提示（`STREAM_DRAW`/`STATIC_DRAW`/`DYNAMIC_DRAW`）会被记录，`getBufferParameter(…, BUFFER_USAGE)` 可读回。
- **绘制**：`drawArrays`、`drawElements`（UNSIGNED_BYTE、UNSIGNED_SHORT、UNSIGNED_INT；`TRIANGLE_FAN` 与 `LINE_LOOP` 会展开为列表）。`POINTS`、`LINES`、`LINE_STRIP`、`TRIANGLES`、`TRIANGLE_STRIP` 映射到对应的 SDL 图元。
- **纹理**：`createTexture`、`bindTexture`、`activeTexture`、`texParameteri`/`texParameterf`、`getTexParameter`、`texImage2D`（原始像素、`<img>`、`<canvas>`、`ImageData`）、`texSubImage2D`、`generateMipmap`、`deleteTexture`。支持 `UNPACK_FLIP_Y_WEBGL` 与 `UNPACK_PREMULTIPLY_ALPHA_WEBGL`。默认转换为 RGBA8；`RGBA`/`RGB`/`LUMINANCE`/`ALPHA`/`LUMINANCE_ALPHA` 配 `UNSIGNED_BYTE` 可用。浮点纹理（`RGBA32F`、`RGBA16F`、`RGB32F`、`RGB16F`、`RG32F`/`RG16F`、`R32F`/`R16F`，`FLOAT` 或 `HALF_FLOAT` 数据，以及 WebGL 1 的 `OES_texture_float`/`OES_texture_half_float`）映射到 SDL 的 RGBA32F/RGBA16F；深度纹理（`DEPTH_COMPONENT16/24/32F`、`DEPTH_STENCIL`、`DEPTH24_STENCIL8`）映射到 `D32_FLOAT`/`D32_FLOAT_S8_UINT`，可作为采样源或深度附件。压缩纹理通过 `compressedTexImage2D`/`compressedTexSubImage2D` 上传，支持 `WEBGL_compressed_texture_s3tc`（BC1/BC2/BC3）、`WEBGL_compressed_texture_s3tc_srgb` 与 `WEBGL_compressed_texture_astc`；扩展只在设备确实支持对应格式时公布。`texImage2D` 还接受 `<video>` 的最新解码帧与 `createImageBitmap` 产生的 `ImageBitmap`。
- **Framebuffer**：`createFramebuffer`、`bindFramebuffer`（`FRAMEBUFFER`/`READ_FRAMEBUFFER`/`DRAW_FRAMEBUFFER`）、`framebufferTexture2D`（`COLOR_ATTACHMENT0`、`DEPTH_ATTACHMENT`、`DEPTH_STENCIL_ATTACHMENT`）、`checkFramebufferStatus`、`createRenderbuffer`、`bindRenderbuffer`、`renderbufferStorage`（深度/深度模板）、`framebufferRenderbuffer`、`delete*`，以及 `blitFramebuffer`（颜色附件；深度/模板位被接受但不复制）。足以实现渲染到纹理、阴影贴图与后处理。
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
- **查询与栅栏**：对象与入口都存在（`createQuery`/`beginQuery`/`endQuery`/`getQueryParameter`、`fenceSync`/`clientWaitSync`/`waitSync`/`getSyncParameter`），行为是优雅降级：栅栏报告 `ALREADY_SIGNALED`，查询结果为 0。
- **Uniform 块**：`getUniformBlockIndex` 返回 `INVALID_INDEX`，让库回退到独立 uniform；块相关入口为空操作。
- **Transform feedback 与 3D/数组纹理**：入口存在但为空操作，避免特性探测类库抛错。

纹理创建时带完整 mip 链；在 `generateMipmap` 之前采样被钳制到第 0 层。矩阵顶点属性会占用相应数量的连续 location，每个 location 读取一列。未启用的属性由最近一次 `vertexAttrib*f` 的值通过一个小常量缓冲提供。顶点着色器可以像片元着色器一样采样纹理：`MAX_VERTEX_TEXTURE_IMAGE_UNITS` 报告 16，顶点采样器走 set 0，并在每次绘制时通过 `SDL_BindGPUVertexSamplers` 绑定。深度值按后端的 `[0, 1]` 裁剪空间存储，不做 GL 的 `[-1, 1] → [0, 1]` 重映射：`gl_Position.z = 0.5` 写入的深度是 `0.5`（真实 WebGL 为 `0.75`）。

## 未实现

着色器源码里的 uniform buffer 对象、transform feedback（入口为空操作）、立方体贴图、3D/数组纹理、多渲染目标（只写第一个片元输出）与 MSAA/采样缓冲。`blitFramebuffer` 只复制颜色附件，深度/模板位被接受但不复制。使用内建块、用户 uniform 块或 `gl_FragDepth` 的着色器会以编译/链接日志报错。

## 平台

管线按 SDL_GPU 设备接受的着色器格式创建：Vulkan 上用 **SPIR-V**（`SDL_GPU_SHADERFORMAT_SPIRV`，Windows/Linux 选择的后端），macOS 上用 **MSL**（`SDL_GPU_SHADERFORMAT_MSL`，Metal 后端）。两个后端共用同一个 GLSL 前端，因此在一个后端能编译的着色器在另一个上也能编译。如果设备的后端两种格式都不接受——Direct3D 12 需要 DXIL——`getContext("webgl")` 与 `getContext("webgl2")` 都会返回 `null`；2D canvas 与页面其余部分仍然正常。

`npm run gui` 会拉取并构建 glslang 与 SPIRV-Cross，合并进 `gui.a` / `gui.lib`（见[打包与构建](./build.md)）；只用 2D canvas 或 DOM 的程序除了归档体积之外没有运行时开销。

## 测试

- `tests/e2e/gui-webgl.test.ts` 编译 GLSL、绘制带纹理和深度测试的四边形、检查面剔除，并读回像素；它还覆盖混合常量色、动态缓冲 usage、顶点着色器纹理采样、`gl_FragDepth` 诊断和上下文丢失/恢复。
- `tests/e2e/gui-webgl2.test.ts` 覆盖 WebGL 2 的新增能力：`#version 300 es` 着色器、VAO 往返、带 divisor 的实例化绘制、`texStorage2D`、`clearBufferfv`、缓冲拷贝、采样器对象、结构体 uniform 字段、`drawingBufferStorage`，以及模板测试、浮点纹理、深度纹理、带 scissor 的清除、framebuffer blit、压缩纹理与 `ImageBitmap`。
- `tests/e2e/gui-webgl2-formats.test.ts` 单独覆盖模板、浮点/深度纹理、scissor 清除、framebuffer blit、压缩纹理与 `ImageBitmap`。
- `tests/e2e/gui-media.test.ts` 还会把 `<video>` 的最新解码帧上传为纹理并读回像素。
- `examples/gui/webgl-cube` 是一个旋转、受光、带纹理的立方体；它在浏览器中原样运行，并由 `tests/e2e/gui-example-webgl.test.ts` 覆盖。
- `examples/gui/webgl2-cubes` 用一次绘制调用画出 64 个实例化立方体，使用 `#version 300 es` 与 VAO；由 `tests/e2e/gui-example-webgl2.test.ts` 覆盖。

这些用例会在宿主选择的后端上运行：Linux（与 Windows）上是 Vulkan，macOS 上是 Metal，因此 SPIRV-Cross → MSL 路径与 SPIR-V 路径受同一组断言保护。

## three.js

`.js` 资源加载器现在只修补 three.js 的着色器循环展开；`WebGLRenderer` 在本引擎真正的 WebGL 2 后端上原样运行，**不再打包 Canvas2D 回退**。已用 three.js r160 通过 SDL_GPU 验证：`MeshBasicMaterial`、`MeshStandardMaterial`、`MeshPhongMaterial`、`CanvasTexture`、线性雾、环境光/平行光/点光/聚光/半球光、`Sprite`、`Line`、`Points` 与 `InstancedMesh`。

本引擎子集之外的能力仍无法模拟：阴影贴图（深度纹理）、立方体贴图/环境贴图、HDR/浮点渲染目标、多渲染目标、transform feedback 与压缩纹理。需要这些能力的场景无法正确渲染；此前 Canvas2D 回退能显示的内容现在全部走 GPU。