# WebGL（由 GPU 绘制的 `<canvas>`）

> 语言 / Language：[English](../../gui/webgl.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md)；引擎总览、已定决策与路线图都在那里。

`<canvas>` 可以申请 WebGL 上下文，而不是 2D 上下文：

```ts
const canvas: any = document.getElementById("scene");
const gl: any = canvas.getContext("webgl"); // 或 "experimental-webgl"
if (gl == null) {
  // 该平台的 GPU 后端无法消费 WebGL 的着色器
}
```

2D 上下文把图形光栅化到 CPU 位图再上传为纹理；WebGL 上下文则**在 GPU 上渲染**：顶点变换、光照、纹理采样、深度测试与混合全部在 SDL_GPU（Vulkan）管线中执行。页面直接合成该上下文的颜色纹理，因此显示时不需要把像素读回 CPU。

`getContext("webgl2")` 返回 `null`；WebGL 2 尚未实现。

## 一帧如何到达屏幕

- 每个上下文拥有一张离屏 `R8G8B8A8_UNORM` 颜色纹理（绘图缓冲）和一张 `D32_FLOAT` 深度纹理，尺寸取自 canvas 的 `width` / `height` 属性。`canvas.width = …` 会重建它们并丢弃内容，与 DOM 规则一致。
- 上下文记录到自己的 SDL_GPU 命令缓冲。`clear`/`draw*` 保持一个渲染 pass 打开；缓冲区/纹理上传或切换 framebuffer 会结束它，并以 `LOAD` 开始新 pass，因此绘图缓冲在多次绘制之间保留，和 WebGL 一样。
- 每帧一次，在页面自己的渲染 pass 之前，引擎提交所有 WebGL 命令缓冲。随后图像管线把颜色纹理当作普通 `PaintImage` 采样，没有 CPU 拷贝。
- WebGL 原点在左下，SDL_GPU 渲染目标原点在左上，因此帧缓冲按自上而下存储：转换 `viewport`/`scissor`，`readPixels` 再把行翻回去，合成器看到的是正立的纹理。
- 固定功能状态变化（`blendFunc`、`depthFunc`、`cullFace`、`colorMask` 等）会改变所需的 `SDL_GPUGraphicsPipeline`，因此管线按（程序、顶点布局、状态）惰性创建，并缓存在程序上。

## 着色器：GLSL ES 到 SPIR-V

SDL_GPU 不能在运行时编译 GLSL，其 Vulkan 后端消费 SPIR-V，所以引擎静态链接了 **glslang**（打进 `gui.a`）。流程是：

1. `shaderSource` 保存 GLSL ES 文本；`compileShader` 先翻译一次，供 `COMPILE_STATUS` / `getShaderInfoLog` 使用。
2. `linkProgram` 用共享的属性/`varying` 位置表再翻译两个阶段，用 glslang 编译，并创建 `SDL_GPUShader`。
3. 反射生成的 SPIR-V，取得 uniform 块精确的 std140 偏移、数组步长与矩阵步长——源码里数组长度可以写宏，glslang 已经解析好了。

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

两个阶段的非采样器 uniform 会合并成同一个块，并在每个阶段各声明一次（顶点 set 1、片元 set 3），与 SDL_GPU 的 Vulkan 描述符布局一致。`uniformMatrix4fv` 按列主序写入反射出的偏移；对采样器调用 `uniform1i` 时记录纹理单元。每次绘制都用动态 uniform 缓冲接口推送该块。

翻译器的限制：结构体 uniform、用户 uniform 块、预处理后仍无法确定长度的 uniform 数组、采样器数组、立方体贴图与多片元输出都会被拒绝（着色器/链接日志会说明）。`#ifdef` 内的代码没问题；非采样器 uniform 声明会被移到外层顶层位置，保证块是无条件的。

## 支持的 API

- **上下文**：`getContext("webgl")`、`canvas`、`drawingBufferWidth/Height`、`getParameter`、`getError`、`getSupportedExtensions`、`getExtension`、`getContextAttributes`、`isContextLost`、`getShaderPrecisionFormat`，以及模板入口（接受状态；深度目标没有模板附件）。
- **着色器与程序**：`createShader`、`shaderSource`、`compileShader`、`getShaderParameter`、`getShaderInfoLog`、`getShaderSource`、`deleteShader`、`createProgram`、`attachShader`、`detachShader`、`linkProgram`、`getProgramParameter`、`getProgramInfoLog`、`getAttachedShaders`、`validateProgram`、`deleteProgram`、`useProgram`、`getActiveUniform`/`getActiveAttrib`、`getUniformLocation`、`getAttribLocation`、`bindAttribLocation`。
- **Uniform**：`uniform1f`…`uniform4f`、`uniform1i`…`uniform4i`、`uniform1fv`…`uniform4fv`、`uniform1iv`…`uniform4iv`、`uniformMatrix2fv`/`3fv`/`4fv`、`getUniform`。
- **缓冲与顶点属性**：`createBuffer`、`bindBuffer`、`bufferData`、`bufferSubData`、`deleteBuffer`、`getBufferParameter`、`enableVertexAttribArray`、`disableVertexAttribArray`、`vertexAttribPointer`、`vertexAttrib1f`…`4f` / `1fv`…`4fv`、`getVertexAttrib`。
- **绘制**：`drawArrays`、`drawElements`（UNSIGNED_BYTE、UNSIGNED_SHORT、UNSIGNED_INT；`TRIANGLE_FAN` 与 `LINE_LOOP` 会展开为列表）。`POINTS`、`LINES`、`LINE_STRIP`、`TRIANGLES`、`TRIANGLE_STRIP` 映射到对应的 SDL 图元。
- **纹理**：`createTexture`、`bindTexture`、`activeTexture`、`texParameteri`/`texParameterf`、`getTexParameter`、`texImage2D`（原始像素、`<img>`、`<canvas>`、`ImageData`）、`texSubImage2D`、`generateMipmap`、`deleteTexture`。支持 `UNPACK_FLIP_Y_WEBGL` 与 `UNPACK_PREMULTIPLY_ALPHA_WEBGL`。全部转换为 RGBA8；接受 `RGBA`、`RGB`、`LUMINANCE`、`ALPHA`、`LUMINANCE_ALPHA` 配 `UNSIGNED_BYTE`。
- **Framebuffer**：`createFramebuffer`、`bindFramebuffer`、`framebufferTexture2D`（颜色附件）、`checkFramebufferStatus`、`createRenderbuffer`、`bindRenderbuffer`、`renderbufferStorage`（深度）、`framebufferRenderbuffer`、`delete*`。足以实现渲染到纹理、阴影贴图与后处理。
- **状态**：`enable`/`disable`/`isEnabled`（`BLEND`、`DEPTH_TEST`、`CULL_FACE`、`SCISSOR_TEST`；模板状态接受但不生效）、`clearColor`/`clearDepth`/`clearStencil`、`clear`、`blendFunc`/`blendFuncSeparate`、`blendEquation`/`blendEquationSeparate`、`depthFunc`、`depthMask`、`colorMask`、`cullFace`、`frontFace`、`viewport`、`scissor`、`pixelStorei`、`finish`。
- **读回**：`readPixels`（RGBA/UNSIGNED_BYTE），可从默认帧缓冲或绑定的 framebuffer 读取。它会阻塞到 GPU 完成；用于测试和截图，不要每帧调用。

纹理创建时带完整 mip 链；在 `generateMipmap` 之前采样被钳制到第 0 层。矩阵顶点属性会占用相应数量的连续 location，每个 location 读取一列。未启用的属性由最近一次 `vertexAttrib*f` 的值通过一个小常量缓冲提供。

## 未实现

WebGL 2（`getContext("webgl2")` 为 `null`）、顶点数组对象、实例化、着色器源码里的 uniform buffer 对象、立方体贴图、3D 纹理、浮点/半浮点纹理、深度纹理、压缩纹理、多渲染目标、MSAA/采样缓冲、模板操作（状态接受，但不写模板缓冲）、常量混合颜色、`drawingBufferStorage`、上下文丢失，以及带 scissor 的 `clear`（clear 会清空整个附件）。使用内建块、整数顶点属性或超出支持范围的 `gl_FragDepth` 的着色器会以编译/链接日志报错。

引擎报告的顶点纹理单元数量为 0，因此行为良好的 WebGL 1 程序不会在顶点着色器里采样纹理。

## 平台

运行时着色器编译面向 **Vulkan SPIR-V**，管线用 `SDL_GPU_SHADERFORMAT_SPIRV` 创建。如果设备的 SDL_GPU 后端是 Metal 或 Direct3D 12（且不接受 SPIR-V），`getContext("webgl")` 返回 `null`；2D canvas 与页面其余部分仍然正常。Windows 会选择 Vulkan（`win.driver()` 会报告），这是已验证的路径。

`npm run gui` 会拉取并构建 glslang，合并进 `gui.a` / `gui.lib`（见[打包与构建](./build.md)）；只用 2D canvas 或 DOM 的程序除了归档体积之外没有运行时开销。

## 测试

- `tests/e2e/gui-webgl.test.ts` 编译 GLSL、绘制带纹理和深度测试的四边形、检查面剔除，并读回像素。
- `examples/gui/webgl-cube` 是一个旋转、受光、带纹理的立方体；它在浏览器中原样运行，并由 `tests/e2e/gui-example-webgl.test.ts` 覆盖。

## three.js

`.js` 资源加载器仍会识别 three.js，但它导出的 `WebGLRenderer` 现在会先探测 WebGL 上下文：有这个后端时页面会得到 three.js 自己的渲染器，只有没有 WebGL 上下文的设备才回退到内置的 Canvas2D 场景渲染器。较新的 three.js 版本要求 **WebGL 2**，因此固定到这些版本的页面即使探测成功也还不能运行；在实现 WebGL 2 之前，请使用 three.js 的 WebGL 1 构建（或回退渲染器）。
