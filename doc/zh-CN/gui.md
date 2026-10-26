# xbintsc GUI 扩展（自托管 HTML/CSS 渲染器）

> 语言 / Language：[English](../gui.md) | **简体中文**

状态：**M17** —— 功能（M1–M15）已全部完成：HTML 解析、CSS 选择器匹配、层叠（cascade）、计算样式与布局（块级、行内、Flexbox 与 CSS 定位）均已就位，引擎还会**绘制**：它构建一个由矩形、图像与排版后的文本 run 组成的显示列表，并通过 SDL_GPU 渲染。输入事件会经过命中测试并投递给原生 TS 处理器，`:hover`/`:focus` 会被动态匹配，`<img>` 依据其固有尺寸确定大小并从纹理绘制——位图（PNG/JPEG/…）与 SVG 文件皆可，后者按绘制尺寸光栅化，因此矢量图保持清晰——CSS transition 会为绘制属性做动画。**M16** 让内联 `<svg>` 子树获得同样的处理：子树被序列化回 SVG 文档，并按绘制设备尺寸光栅化，因此图标不再需要单独的文件。**M8** 增加了交互式 DOM：具有稳定标识的元素句柄、变更操作（`appendChild`、`textContent`、`classList`、`style` 等）以及支持捕获/冒泡的元素级事件。**M9** 会提前编译 `<script>` 主体（内联与 `<script src>`）——参见 `gui-scripts.md`。**M10** 增加 `requestAnimationFrame` 以及若干 DOM 辅助方法。**M11** 增加 `position: relative`/`absolute`/`fixed` 及其偏移，`examples/gui/pelican-bike`（一个可玩的 2D 游戏）就构建在它之上。**M12** 明确了 DOM 节点生命周期（哪些变更释放、哪些保留），并让引擎内存有界：参见[节点生命周期](./gui/dom.md)与[内存与 GC](./gui/runtime.md)。**M13** 真正播放
`<video>`/`<audio>`——可选的 `media` 扩展用 vendored 的最小 FFmpeg 在主线程解码，
视频帧走图像纹理路径、音频进入 SDL 流，DOM 媒体 API 暴露
`play`/`pause`/`currentTime`/… 与媒体事件。只启用 `gui` 不会链接 FFmpeg。**M14** 增加了 `<canvas>`，配一个自研的 2D 软件光栅器（抗锯齿
填充、路径、文本与 `drawImage`），并经图像纹理路径上传——参见
[Canvas](./gui/canvas.md)。**M15** 增加了 CSS `transform` /
`transform-origin`：每个布局盒一个合成好的 `Matrix2D`，由显示列表一路带到
渲染器，因此盒子（以及其中的文本、图像与子元素）可以被平移、缩放与旋转而
不触发任何重排——再加上 `transition: transform`，这些姿态可以动画（自绘的示例
`examples/gui/pelican-svg` 正是用它让矢量画动起来）。参见
[布局](./gui/layout.md#变换m15)与[动画](./gui/animation.md#css-transformm15)。**M17** 增加了真正的 **WebGL 1.0**：`<canvas>.getContext("webgl")` 用内置的 glslang 把页面的 GLSL ES 编译成 SPIR-V，并通过 SDL_GPU 渲染，因此几何、光照、纹理采样与混合都在 GPU 上完成，页面直接合成颜色纹理而无须读回——见 [WebGL](./gui/webgl.md)。本总览记录了锁定决策、架构、里程碑计划与当前进度；引擎本身按功能拆分在以上各页中。

## 目录

- [API](./gui/api.md) —— 窗口句柄、事件与诊断
- [DOM、HTML 与 CSS](./gui/dom.md) —— 解析、层叠、交互式 DOM 与 AOT 脚本
- [布局](./gui/layout.md) —— 块级、行内、Flexbox 与定位
- [绘制](./gui/rendering.md) —— 显示列表、GPU 管线与文本
- [媒体](./gui/media.md) —— 图像、SVG、视频与音频
- [Canvas](./gui/canvas.md) —— `<canvas>` 与 2D 上下文
- [输入](./gui/input.md) —— 命中测试、事件与 `:hover`/`:focus`
- [动画](./gui/animation.md) —— `requestAnimationFrame`、CSS transition 与 transform
- [运行时集成](./gui/runtime.md) —— 事件循环、原生桥接与内存
- [打包与构建](./gui/build.md) —— 构建 `gui.a` 与运行示例
- [已知问题与待决问题](./gui/known-issues.md)

## 目标

1. **跨平台 GUI**：由 xbintsc 把 TypeScript 编译为原生二进制：macOS、Linux、Windows。
2. **HTML5/CSS 渲染**，使用**自研**引擎（解析器、层叠、布局、绘制），而非系统 WebView 或嵌入式浏览器。
3. **GPU 加速**——硬件光栅化/合成是硬性要求，而不是优化项。
4. **多窗口。**
5. 引擎不得损害 xbintsc 作为二进制编译器的本质：核心编译器、词法器、解析器、绑定器与代码生成器必须保持平台无关，绝不滋生 GUI 分支。

## 非目标（目前）

- 执行**运行时**页面 `<script>`（通过网络获取或动态创建的脚本）。**编译期**脚本由 xbintsc AOT 编译，并且确实会运行——参见 `gui-scripts.md`。逻辑也可以写在通过 `xt_call_with_this` 回调的原生 TS 中。
- JS 引擎（QuickJS/V8/...）。明确不在范围内；没有运行时解释器/JIT，因此 `<script>` 主体会提前编译。
- 完整的 Web 兼容性 / 浏览器。我们实现一个实用的 HTML/CSS 子集。

## 锁定决策

| # | 决策 |
| --- | --- |
| 1 | **没有页面 JS 引擎。** 行为由原生 TS 实现，通过 `xt_call_with_this` 回调。`<script>` 主体由 xbintsc 自身 **AOT 编译**（无解释器）——参见 `gui-scripts.md`。 |
| 2 | 允许使用第三方**底层**库（GPU 后端、文本整形、图像解码）。HTML/CSS 解析 + 布局 + 绘制调度均为自研。 |
| 3 | **GPU 加速是强制要求。** |
| 4 | 引擎**非自托管**（它是 C/C++，不是 TS），但不得影响编译器的平台无关设计。以按平台预构建的归档形式交付，通过 `nativeObjects` 链接。 |
| 5 | **事件循环是通用的**：运行时暴露一个通用主循环钩子与一个轮询原语；任何 GUI 特定的东西都不会进入 `runtime/`。 |
| 6 | 核心对象模型支持**多窗口**。 |
| 7 | 第三方底层库**静态链接进扩展归档**，使发布版保持自包含；链接时只额外加入操作系统框架。`gui.a` 携带 SDL3/FreeType/HarfBuzz，可选的 `media.a` 携带 FFmpeg，仅在 `--ext media` 时链接。 |

## 架构

```
              TypeScript（由 xbintsc 编译为原生代码）
                │  import { createWindow, run } from "gui"
                ▼
        gui 扩展绑定  (src/extensions/gui)
                │  xt_gui_* 符号  （统一的 (argc, argv) ABI）
                ▼
             gui.a                  ── 自研引擎（C/C++）
   ┌───────────────┬──────────────────┬───────────────────┐
   ▼               ▼                  ▼                   ▼
 HTML 解析器    CSS 层叠 +         布局                GPU 合成器
 （子集）       选择器匹配     （块级/行内/flex）  （SDL_GPU: Metal/
                                                      Vulkan/D3D12）
                │
                ▼
        窗口 + 输入（SDL3）── 多窗口
        文本（HarfBuzz + FreeType）── 字形图集 / SDF
        图像（stb_image；SVG 经 NanoSVG）
        媒体 DOM + 音频 sink ── 可选的 media.a（FFmpeg）安装解码器
```

引擎从不与编译器通信。编译器只看到一个带有 `nativeObjects()`、`linkerFlags()` 和 `modules()` 的 `Extension`。

## 技术栈（已确认）

| 关注点 | 选择 | 理由 |
| --- | --- | --- |
| 窗口 + 输入 + 多窗口 | **SDL3** | 跨平台窗口、HiDPI、IME、剪贴板、拖放、关闭/隐藏事件 |
| GPU | **SDL_GPU**（SDL3） | 一条覆盖 Metal / Vulkan / D3D12 的渲染路径；避免三套后端 |
| 文本整形 | **HarfBuzz** | 正确处理复杂文种的整形 |
| 字形光栅化 | **FreeType** | 字形轮廓 → GPU 图集 / SDF |
| 图像 | 位图用 **stb_image**（vendored 头文件），SVG 用 **NanoSVG**（vendored 头文件） | 起步阶段用单头文件；SVG 文件按需光栅化 |
| 视频 + 音频 | **FFmpeg**（静态 vendored，最小化构建，可选 `media` 扩展） | 解码常用容器/编码；不依赖 WebView 或平台媒体框架 |

固定版本：SDL3 `release-3.2.10`、FreeType `2.13.3`、HarfBuzz `10.1.0`、
FFmpeg `7.1`（供可选的 `media` 归档使用；可用 `SDL3_TAG` /
`FREETYPE_VERSION` / `HARFBUZZ_VERSION` / `FFMPEG_VERSION` 覆盖）。

如果否决 SDL3，备选是 **GLFW + OpenGL 3.3**（更简单，但 OpenGL 在 macOS 上
已弃用，且不提供现代 GPU 抽象）。如果引擎用 Rust 编写，则用 **wgpu-native**。

> 这些 vendored 库会**静态链接进 `gui.a` / `media.a`**，因此发布的 xbintsc 保持
> “下载即用”；`linkerFlags()` 只添加操作系统框架。

## 里程碑

1. **M1 — 基础** ✅
   - 运行时中的通用 `xt_loop_poll` / `xt_loop_set_main`。
   - `gui` 扩展骨架 + 通用 CLI 扩展注册。
2. **M2 — 窗口 + GPU 清除** ✅
   - SDL3 窗口、SDL_GPU swapchain、多窗口、主循环集成。
   - `createWindow` / `run` / `quit` + `on`/`off` 窗口方法端到端可用
     （`runtime/ext_gui/`、`scripts/build-gui.ts`）。
3. **M3 — HTML/CSS 子集** ✅
   - **M3a — 解析 + 层叠** ✅ HTML 解析器、DOM 树、CSS 解析器、选择器匹配、
     UA/作者/内联层叠、继承、计算样式
     （`dom.*`、`css.*`、`style.*`、`document.*`）。
   - **M3b — 布局** ✅ 块级/行内流 + Flexbox（`layout.*`）、
     `getBoundingClientRect`/`layoutTree`。
4. **M4 — 绘制 + 文本 + 显示列表**
   - **M4a — 显示列表 + GPU 图形** ✅ 背景/边框显示列表、圆角矩形 SDL_GPU
     管线（`paint.*`、`renderer.*`）。
   - **M4b-1 — 文本技术栈 + 度量** ✅ HarfBuzz + FreeType 链接进 `gui.a`、
     字体解析/缓存、供布局使用的基于整形的文本度量
     （`text.*`、`measureText`/`fontMetrics`）。
   - **M4b-2 — 字形渲染** ✅ FreeType 光栅化、共享的 shelf 打包字形图集、
     显示列表中的带纹理文本四边形与感知 HiDPI 的光栅缩放。渐变仍待完成。
5. **M5 — 输入 + 事件** ✅
   - 命中测试（`LayoutTree::hitTest`）、带负载投递给 TS 处理器的指针/滚轮/
     键盘事件、`:hover`/`:focus` 有状态匹配与重算样式
     （`css.*`、`style.*`、`document.*`、`gui.cpp`），以及
     `hitTest`/`sendEvent` 测试钩子。
6. **M6 — 图像，然后是 CSS transition/动画**
   - **M6a — 图像** ✅ stb_image 解码（位图）与 NanoSVG 光栅化（SVG 文件，按
     绘制像素尺寸）、`<img>` 替换元素布局、按文件 GPU 纹理与带纹理四边形
     （`image.*`、`svg.*`、`paint.*`、`renderer.*`）。
   - **M6b — transition/动画** ✅ `transition` 简写 + 长写、对
     `background-color`/`color`/`border-color`/`border-radius` 的动画、重定向
     与 `win.advance(ms)`。`@keyframes` 仍待完成。
7. **M7 — CI 与发布** ✅（Linux/macOS 构建） / 🚧（运行 + Windows arm64）
   - `compile-examples` 在 Linux 与 macOS 上构建 `gui.a` 与 `media.a`（必需），
     然后在 Linux 上的 Xvfb + lavapipe 下运行示例与 `tests/e2e/gui-*.test.ts`
     （必需）。macOS 运行在确认 WindowServer 之前为试验性；它被保护起来，失败
     只记录日志而不标注运行。
   - `package` job 在组装发布之前构建 `gui.a`/`gui.lib` 与 `media.a`/`media.lib`，
     因此两者都会随现有 runtime 归档一起发布（`package-release` 复制整个
     `runtime/`）。Windows x64 已包含；Windows arm64 仍跳过。
   - `vendor/`（GUI 归档的 SDL3/FreeType/HarfBuzz，以及 `media` 的 FFmpeg，
     缓慢的部分）按 OS/arch 缓存，以 `scripts/build-gui.ts` /
     `scripts/build-media.ts` 为键。Windows x64 有独立的源码 + FFmpeg 安装前缀
     缓存。
   - Windows x64 在 CI 中构建两个归档并链接一个 `gui,media` 程序；托管 runner
     没有 GPU/Vulkan 驱动，因此 GUI/媒体示例的*运行*仍为试验性（失败只记录
     日志而不标注运行）。Windows arm64 仍跳过两个归档（见
     [待决问题](./gui/known-issues.md)）。CI 通过
     `.github/actions/vs-toolchain` → `scripts/setup-vs-toolchain.ps1` 使用与
     本地构建相同的 Visual Studio MSVC + 自带 LLVM 工具链。
8. **M8 — 交互式 DOM** ✅
   - 具有稳定标识的元素/文档句柄、通过运行时访问器读写属性、遍历/特性/查询、
     带有延迟重算样式/重新布局的变更
     （`dom_api.*`、`document.*`、`dom.*`、`gui.cpp`、`window.cpp`）、具有
     捕获 + 冒泡阶段与 `stopPropagation` 的元素事件，以及窗口级 `e.target`
     兼容性。无编译器改动。参见 `gui-scripts.md`。
9. **M9 — AOT `<script>`** ✅
   - **M9a** ✅ —— 通用 `Extension.assetLoaders` 钩子 + bundler 集成。
   - **M9b** ✅ —— gui `.html` 资产加载器（`src/extensions/gui/html.ts`）：
     内联主体被包装进 `__xt_script_<hash>(window, document)` 函数，通过
     `__registerScript` 内置函数注册，并替换为
     `<script data-xt-id="<hash>">` 标记；`win.loadHTML` 在解析后运行匹配的
     函数并触发 `DOMContentLoaded` 然后是 `load`。
   - **M9c** ✅ —— `<script src>` 文件相对 HTML 读取，其顶层 import 被提升
     （说明符从 HTML 目录重写），其主体被包装/注册；缺失文件与 import 绑定
     冲突会成为诊断。一切都按文档顺序运行。
10. **M10 — 打磨** ✅
    - `win.requestAnimationFrame(fn)` / `win.cancelAnimationFrame(id)`；回调
      在每帧顶部以帧时间戳运行，并可修改 DOM
      （`gui.cpp`、`window.cpp`、`gui_engine.h`）。
    - `Element.offsetWidth` / `offsetHeight`（取整后的边框盒，会冲刷待处理
      的变更）与 `Element.contains(other)`（`dom_api.cpp`）。
11. **M11 — CSS 定位** ✅
    - `position: relative` / `absolute` / `fixed` 与 `top`/`right`/`bottom`/
      `left`，相对最近的已定位祖先解析（没有时相对视口）；`auto` inset 保留
      静态位置；脱离文档流的 `width: auto` 使用 shrink-to-fit，两个水平 inset
      都给出时则填满可用宽度（`layout_flow.cpp`、`layout.h`）。
    - flex 算法现在会计入项的外边距，负外边距参与布局而不再被丢弃
      （`layout_flex.cpp`）。
    - `examples/gui/pelican-bike` 基于它实现，e2e 覆盖见
      `tests/e2e/gui-layout.test.ts`。
12. **M12 — 节点生命周期 + 内存** ✅
    - 明确区分变更释放与保留的内容：`innerHTML`/`inner`/`textContent`/
      `replaceChild` 会释放被丢弃的子树（`XtDocument::discard`、
      `domapi::discard_subtree`），而 `removeChild`/`remove` 保留 DOM 语义
      （`document.*`、`dom_api_*.cpp`）。
    - 句柄槽位带"每槽代次"地复用，过期句柄保持惰性失效，同时句柄表不再随
      变更次数增长（`dom_api.cpp`、`gui_engine.h`）。
    - 收集器触发阈值改为跟随存活集而非只升不降，避免泄漏或存活集尖峰演变成
      内存失控（`runtime/xt_alloc.c`）。`XT_GC_TRACE=1` 会报告每次回收。
    - `win.stats()` 暴露引擎计数器；e2e 覆盖见
      `tests/e2e/gui-retention.test.ts`（见[内存与 GC](./gui/runtime.md)）。
13. **M13 — 视频与音频** ✅
    - `<video>` / `<audio>` 通过可选的 `media` 扩展（`runtime/ext_media/media.cpp`）
      使用静态 vendored 的最小 FFmpeg 解码，帧沿用现有图像管线上传到 GPU 纹理，
      音频由 GUI 的音频 sink 在主线程推送进 SDL 流，并在元素句柄上暴露 DOM
      媒体 API 与 `loadedmetadata`/`play`/`pause`/`ended`/`timeupdate`/`error`
      事件（`dom_api_media.cpp`、`dom_api_proto.cpp`）。由 TS + CSS 控制；没有
      内置控件。示例见 `examples/gui/media.*`；e2e 见
      `tests/e2e/gui-media.test.ts`（启用 `media`）与
      `tests/e2e/gui-media-disabled.test.ts`（不启用）。
14. **M14 — `<canvas>` 2D** ✅
    - `<canvas>` 是一个替换元素（`xtgui::Canvas2D`、`layout.*`），
      `getContext("2d")` 句柄与绘制 API 位于 `dom_api_canvas.cpp`，自研的抗锯齿
      软件光栅器位于 `canvas_raster.cpp`，按窗口的生命周期位于 `gui_canvas.cpp`。
      位图经图像路径上传为 GPU 纹理（`renderer_image.cpp`），显示列表携带
      `canvas_id`（`paint.*`）。参见 [Canvas](./gui/canvas.md)；e2e 见
      `tests/e2e/gui-canvas.test.ts`。
15. **M15 — CSS `transform`** ✅
    - `transform`/`transform-origin` 解析为 `TransformOp` 列表
      （`transform.{h,cpp}`、`style.*`），和其他绘制属性一样参与 transition
      （`document.*`），并在布局之后合成为每个盒子的 `world` 矩阵及其逆矩阵
      （`layout.*`）。绘制把矩阵挂在每个 op 上，渲染器按矩阵映射四边形顶点，
      因此着色器无需改动。命中测试与 `getBoundingClientRect` 同样跟随。参见
      [布局](./gui/layout.md#变换m15)与[动画](./gui/animation.md#css-transformm15)；
      e2e 见 `tests/e2e/gui-transform.test.ts` 与
      `tests/e2e/gui-example-svg.test.ts`。
16. **M16 — 内联 `<svg>`** ✅
    - `<svg>` 是替换元素（`layout.*`）：固有尺寸先取 `width`/`height` 属性，
      其次 `viewBox`，最后 300x150；常规盒树遍历不再进入其子元素。
    - `inline_svg.{h,cpp}` 把子树序列化为独立 SVG 文档，并还原 HTML 解析器
      小写掉的、区分大小写的名称（`viewBox`、`preserveAspectRatio`、
      `linearGradient`、`gradientUnits` 等）。
    - 绘制把 markup 挂在图像 op 上（`PaintImage::svg`），渲染器按绘制设备尺寸、
      通过与 SVG 文件相同的 NanoSVG 缓存（`svg.{h,cpp}`）解析并光栅化；markup
      缓存有界，DOM 变更会在下一次布局时重新序列化。e2e 见
      `tests/e2e/gui-inline-svg.test.ts`。
17. **M17 — WebGL** ✅
    - `canvas.getContext("webgl")` 创建真正的 GPU 上下文（`webgl.{h,cpp}`）：
      一张离屏 RGBA8 颜色纹理加一张深度纹理、自己的 SDL_GPU 命令缓冲，并根据
      程序、顶点布局与固定功能状态惰性创建管线。页面通过既有图像路径直接合成
      颜色纹理，不需要 CPU 读回。
    - `webgl_glsl*.cpp` 把 GLSL ES 1.00/3.00 改写为 Vulkan GLSL ES 3.10
      （属性/`varying` 位置、`gl_FragColor`、`texture2D`、每阶段一个 std140
      uniform 块、显式采样器绑定），用静态打包的 glslang 编译，并反射 SPIR-V
      得到 uniform 偏移。
    - `dom_api_webgl*.cpp` 暴露 WebGL 1.0 接口，包括纹理、mipmap、
      渲染到纹理的 framebuffer 与 `readPixels`。见 [WebGL](./gui/webgl.md)；
      e2e 见 `tests/e2e/gui-webgl.test.ts` 与
      `tests/e2e/gui-example-webgl.test.ts`，示例见 `examples/gui/webgl-cube`。

## 进度日志

- **M1** ✅ 通用 `xt_loop_poll`/`xt_loop_set_main`；`gui` 扩展骨架。
- **M2** ✅ SDL3 窗口 + SDL_GPU 清除、多窗口、`createWindow`/`run`。
- **M3a** ✅ HTML 解析器（`dom.*`）、CSS 解析器/匹配器（`css.*`）、层叠与
  计算样式（`style.*`）、文档模型（`document.*`）、
  `computedStyle`/`queryCount`/`documentTree`、e2e 覆盖。
- **M3b** ✅ 布局（`layout.*`）：块级流、带断行的行内格式化上下文、单行
  Flexbox、`getBoundingClientRect`/`layoutTree`、e2e 覆盖。
- **M4a** ✅ 显示列表（`paint.*`）与带 MSL 圆角矩形管线的 SDL_GPU 2D 渲染器
  （`renderer.*`）、`paintList`/`paintCount`、e2e 覆盖。
- **M4b-1** ✅ FreeType + HarfBuzz 获取/构建/合并进 `gui.a`、带字体解析的文本
  模块（`text.*`）、HarfBuzz 整形与 FreeType 度量、布局中的真实文本度量、
  `measureText`/`fontMetrics`、e2e 覆盖。
- **M4b-2** ✅ `renderer.*` 中的字形图集 + 带纹理文本管线、`paint.*` 中的
  整形文本 run、`text.*` 中的 `xt_text_shape_run`/`xt_text_rasterize`、
  HiDPI 光栅缩放、e2e 覆盖。
- **M5** ✅ 命中测试 + 输入事件（`LayoutTree::hitTest`、`xt_dom_describe`、
  `xt_gui_dispatch_*`）、匹配器中的 `:hover`/`:focus` 与动态重算样式、
  `hitTest`/`sendEvent` 测试钩子、e2e 覆盖。
- **M6a** ✅ 图像解码（`image.*`、vendored stb_image）、布局中的 `<img>`
  固有尺寸、显示列表中的 `PaintImage`、按文件 RGBA 纹理与 `renderer.*` 中的
  图像管线、e2e 覆盖。SVG 文件同样可解码（`svg.*`、vendored NanoSVG）：矢量源
  按绘制设备像素尺寸光栅化并按该尺寸缓存，因此缩放与 HiDPI 保持清晰。
- **M6b** ✅ CSS transition（在 `style.*` 中解析、在 `document.*` 中维护动画
  时钟与 transition 状态、`advance`/`advance(ms)` 钩子、e2e 覆盖）。
- **M7** ✅ 在 CI 中于 Linux、macOS 与 Windows x64 上构建 `gui.a`/`gui.lib` 与
  `media.a`/`media.lib`（必需），并在 Linux 的 Xvfb 下运行示例与 GUI e2e 套件；
  `package` 发布这些归档（Windows x64 已包含），`vendor/` 被缓存，`ar -M` 合并
  在 GNU/Linux、macOS 与 COFF/Windows 上可用。macOS 运行、Windows 示例的*运行*
  以及 Windows arm64 在 CI 中仍为试验性。Windows job 通过
  `.github/actions/vs-toolchain` 使用与本地构建相同的 Visual Studio MSVC +
  自带 LLVM 工具链。
- **M8** ✅ 元素/文档句柄（`dom_api.*`）、带延迟重算样式/重新布局的 DOM 变更
  （`document.*`、`xt_gui_flush_dom`）、带捕获/冒泡与 `stopPropagation` 的
  元素事件分发（`dom_api.*`、`gui.cpp`），以及
  `tests/e2e/gui-*.test.ts` 中的 e2e 覆盖。
- **M9a** ✅ 扩展可以按文件扩展名注册资产加载器；
  `bundleModules`/`loadGraph` 在读取文件后查询它们。
  `tests/driver/modules.test.ts` 中的单元测试。
- **M9b** ✅ `import page from "./page.html"` 把内联 `<script lang="ts">` 主体
  编译为启动时注册、由 `win.loadHTML` 运行的 AOT 函数（在首次布局之前；
  `DOMContentLoaded` 然后是 `load`）。`tests/extensions/gui.test.ts` 中的单元
  测试，`tests/e2e/gui-*.test.ts` 中的 e2e。
- **M9c** ✅ 外部 `<script src>` 文件被读取，其 import 被提升（说明符从 HTML
  目录重写），其主体被包装/注册；缺失文件与 import 绑定冲突会成为诊断。
  加载器错误被 `loadGraph` 捕获并报告为构建错误。
- **M10** ✅ 窗口句柄上的 `requestAnimationFrame`/`cancelAnimationFrame`
  （回调在每帧布局之前以帧时间戳运行），以及元素句柄上的
  `offsetWidth`/`offsetHeight`/`contains`；e2e 覆盖在
  `tests/e2e/gui-*.test.ts` 中。
- **M11** ✅ `position: relative`/`absolute`/`fixed` 与 `top`/`right`/`bottom`/
  `left`。布局为每个盒子保留包含块（最近的已定位祖先的 padding box，且只在
  该祖先拥有最终尺寸后才发布，因此 `bottom`/`right` 精确）；绝对定位的盒子
  在正常流内容之后布局，因此不影响它们；实现了 shrink-to-fit / 填满可用宽度
  两种规则；flex 项现在会计入外边距。e2e 覆盖在
  `tests/e2e/gui-layout.test.ts`；`examples/gui/pelican-bike` 基于它实现。
- **M12** ✅ DOM 节点生命周期与内存。`innerHTML`/`textContent`/`replaceChild`
  现在会释放它们丢弃的子树，而不是把它们永久留在分离节点池里；`removeChild`
  保留其文档化的 DOM 语义；句柄槽位会带"每槽代次"地复用，使过期句柄保持惰性
  失效；收集器的触发阈值改为跟随存活集，而不再只升不降。`win.stats()` 暴露
  引擎计数器，`tests/e2e/gui-retention.test.ts` 对其做断言；每帧重写精灵的游戏
  循环现在内存恒定（见[内存与 GC](./gui/runtime.md)）。
- **M13** ✅ `<video>` / `<audio>` 支持。可选的 `media` 扩展 vendored 并构建一个
  最小化的静态 FFmpeg（`scripts/build-media.ts`），合并进 `media.a`；
  `runtime/ext_media/media.cpp` 在主线程把视频解码为 RGBA 帧、把音频解码为
  48 kHz 立体声 PCM，`runtime/ext_gui/gui_media.cpp` 持有播放器、实现 SDL 音频
  sink 并在每帧推进媒体时钟与事件，`renderer_image.cpp` 把每个新视频帧沿图像纹理
  路径上传，`dom_api_media.cpp` 在元素句柄上暴露 `play`/`pause`/`load` 与媒体
  属性。`gui.a` 不含 FFmpeg；只启用 `media` 而不启用 `gui` 会被注册表拒绝。
  `examples/gui/media.*` 演示 TS + CSS 控件的模式，
  `tests/e2e/gui-media.test.ts` 对其做断言。
- **M14** ✅ `<canvas>` 支持。新的替换元素（`layout.*`）从 `width`/`height`
  属性获得固有尺寸（默认 `300x150`），`xtgui::Canvas2D`（`canvas.{h,cpp}`）
  持有 RGBA8 位图与绘制状态，`canvas_raster.cpp` 是自研的抗锯齿软件光栅器
  （4 条子扫描线 + 水平解析覆盖率、非零环绕），`getContext("2d")` 句柄及其 API
  在 `dom_api_canvas.cpp` 绑定，`gui_canvas.cpp` 持有按窗口的生命周期。位图经
  图像纹理路径上传（`renderer_image.cpp`），显示列表携带 `canvas_id`
  （`paint.*`）。此后绘制面又补齐了线性/径向渐变（`canvas_gradient.cpp`）、
  `ellipse`/`roundRect`、线段端点/连接/斜接上限与虚线模式（`canvas_style.cpp`、
  `canvas_stroke.cpp`）以及 `getTransform`；`transform` 改为后乘，因此
  `translate(x, y); rotate(a)` 围绕平移后的原点旋转，而
  `examples/gui/pelican-canvas` 完全用这些 API 画出会动的鹈鹕骑自行车。参见
  [Canvas](./gui/canvas.md)；e2e 见 `tests/e2e/gui-canvas.test.ts`。
- **M15** ✅ CSS `transform` / `transform-origin`。新的翻译单元
  （`transform.{h,cpp}`）把 `translate`/`translateX`/`translateY`/`scale`/
  `scaleX`/`scaleY`/`rotate`/`matrix`（以及 `translate3d`/`scale3d`，其 `z` 参数被丢弃）
  解析为 `Matrix2D` 列表，经 `computedStyle` 原样报告，并为 transition 插值
  （形状或单位变化则吸附）。布局在常规遍历之后为每个盒子合成 `world` 矩阵与
  逆矩阵（`applyTransforms`），`transform-origin` 按边框盒解析——所以布局几何
  与流式排布永不改变。绘制把矩阵挂到每个形状/文本/图像 op 上，渲染器按矩阵
  映射四边形顶点（不改着色器、不加 uniform）；命中测试把点经逆矩阵映射，
  `getBoundingClientRect` 返回变换后的包围盒。`examples/gui/pelican-svg` 用它
  为自己的画做动画；e2e 见 `tests/e2e/gui-transform.test.ts` 与
  `tests/e2e/gui-example-svg.test.ts`。
- **M16** ✅ 内联 `<svg>`。`buildBoxTree` 把 `<svg>` 元素当作替换盒（固有尺寸
  依次取 `width`/`height`、`viewBox`、300x150）并停止下探，
  `inline_svg.{h,cpp}` 以规范 SVG 大小写序列化子树，绘制把 markup 放进
  `PaintImage::svg`，`renderer_image.cpp` 通过 NanoSVG 缓存（`svg.{h,cpp}`，
  markup 采用有界 LRU）按绘制设备像素尺寸光栅化。`win.inlineSvg` 诊断钩子
  暴露序列化文档，`tests/e2e/gui-inline-svg.test.ts` 固定了尺寸、命中测试、
  渐变、CSS 盒样式与 DOM 变更。

