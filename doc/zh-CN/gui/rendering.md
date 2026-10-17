# 绘制、GPU 与文本

> 语言 / Language：[English](../../gui/rendering.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

## 绘制与显示列表（M4a/M4b）

`runtime/ext_gui/paint.{h,cpp}` 按画家顺序遍历布局树，生成一个与后端无关的
`DisplayList`：一个按画家顺序排列的 `ops` 列表，包含**矩形**（背景与四条实心
边框，带 `border-radius`）、**文本 run**（每个携带其文本、颜色与已解析的
`FontSpec`）与**图像**，背后由并列的负载数组支撑。每个 op 还携带当前生效的裁剪
矩形（最近的 `overflow: hidden` 祖先的 padding box），因此 `overflow` 裁剪与
`z-index` 顺序都能保留到渲染器。`DisplayList::dump()` 为
`paintList()`/`paintCount()` 提供数据。

`opacity` 只在两端生效：计算值为 `0` 的盒子（及其整棵子树）不绘制任何内容，
大于 `0` 的一律按完全不透明绘制。图形只携带其颜色自身的 alpha，因此没有部分
透明盒子的合成——CSS 解析器接受的 `opacity` 过渡因此也看不到效果。把隐藏态写成
`opacity: 0` 来隐藏/显示一层，正是 `examples/gui/pelican-bike` 游戏结束遮罩的做法。

`runtime/ext_gui/renderer.{h,cpp}` 把该列表转换为每个窗口的批处理顶点缓冲
（图形、字形四边形与图像），并把管线、裁剪与纹理都相同的连续 op 合并为
**绘制 run**。每个 run 用各自的 GPU scissor 矩形（按设备像素缩放）绘制，因此
画家顺序与 `overflow` 裁剪按 op 生效，并通过 SDL_GPU 图形管线完成：

- 共享管线会依据设备支持的着色器格式延迟创建。SDL_GPU 的三个后端各自只接受一种
  二进制格式，且都不会在运行时编译 GLSL/HLSL，因此引擎三种都随包提供，由
  `selectShader`（`renderer_shaders.h`）挑选设备可用的那一种：Metal 用 **MSL**
  （SDL 从内嵌源码编译）、Vulkan 用 **SPIR-V**
  （`runtime/ext_gui/spirv/*.{vert,frag}`，由 `glslc` 编译）、Direct3D 12 用
  **DXIL**（`shaders.hlsl`，由 `dxc` 编译）。
  `scripts/build-gui-shaders.mjs` 重新生成内嵌二进制块，产物已提交，因此构建引擎
  无需额外工具。各格式的描述符绑定不同——SPIR-V 遵循 `SDL_gpu_vulkan.c` 的布局
  （uniform 在 `set=1, binding=0`，采样器在 `set=2, binding=0`，纹理在
  `set=2, binding=1`）——且 glslc 会把所有入口点命名为 `main`，而 MSL/DXIL 保留
  各自的描述性名称。
  需注意：SDL 3.2.10 的 Direct3D 12 后端无法创建着色器中声明了 uniform buffer 的
  图形管线（返回 `E_INVALIDARG`）；引擎正是用这种方式传视口，因此 DXIL 这条路已
  构建但在上游修复前不可用。
- 图形片元着色器中的**圆角矩形距离场**提供抗锯齿填充；顶点携带
  `position`、`local`、`half extents`、`radius` 与颜色，一个视口尺寸的
  push constant 负责投影。启用 alpha 混合。
- 文本管线采样一张**单一共享的灰度字形图集**（`R8_UNORM`，2048²，
  shelf 打包，LINEAR 过滤），把每个字形绘制为带纹理的四边形
  （`position`、`uv`、颜色），按覆盖率调制 alpha。
- 只有在文档或视口变化（`geometry.dirty`）时才上传几何数据，因此稳态帧
  只需绑定并绘制。

每个 op 还携带它来自的那个盒子的 `Matrix2D`（`runtime/ext_gui/transform.h`、
`PaintRect::transform` 等）。它在构建顶点缓冲时应用，而不是在着色器里：
`pushRect` 把四个角经矩阵映射（距离场的 `local`/半尺寸/半径字段仍留在**盒子**
坐标系，因此圆角旋转后依然是圆的），`pushQuad`/`pushGlyph` 同样映射字形或图像
四边形。这样 MSL/SPIR-V/DXIL 着色器与管线都无需改动——一次变换的代价只是每个
顶点几次乘法，与像素数无关。被变换盒子的裁剪矩形取其内边距盒经变换后的包围盒，
因为 GPU 的 scissor 是轴对齐的。文本是唯一需要多于“乘一下四个角”的情况：
缩放型的变换会放大图集中的位图，因此向图集请求字形时用的是设备缩放**乘以矩阵的
放大倍率**（限制在 0.5–4 倍之间，图集容量有限），再把四边形除回逻辑单位——
这正是 HiDPI 缩放原本就在用的做法。

窗口背景（`setBackground`）是渲染通道的清除色。

M4 仍需完成：渐变。

## 文本技术栈（M4b）

`runtime/ext_gui/text.{h,cpp}` 封装 **HarfBuzz**（整形）与 **FreeType**（度量
与最终光栅化）。两者都静态构建，并由 `scripts/build-gui.ts` 链接进 `gui.a`。

- 字体从常见系统路径解析（macOS 上的 Helvetica/Arial、Linux 上的
  DejaVu/Liberation、Windows 上的 Segoe UI/Arial），可用 `XT_GUI_FONT`
  （以及 `XT_GUI_FONT_MONO`）覆盖，并按 `(字体类别, 字号)` 缓存。目前只使用
  常规直立字面；字重/斜体选择是后续细化。
- **主字面缺少的码点会回退到其他字面。** 引擎维护一条小的回退链——先平台的
  符号字体（Segoe UI Symbol、Apple Symbols、Noto Sans Symbols、DejaVu Sans），
  再是另一字体类别——并为每个码点解析出回退链中第一个拥有该字形的字面。
  run 以每个字面一段的方式整形，因此混合 run（拉丁文加箭头、制表符或几何图形，
  或 CJK 中的拉丁文）会绘制每个字形，而不是把缺失的字形当作 `.notdef` 丢弃。
  这正是让诸如 `▮`/`▯`（U+25AE/U+25AF，鹈鹕自行车速度条所用）这样的字符在
  CJK 主字面没有几何图形时仍能显示的原因。每个整形后的字形携带自己的字面
  （`ShapedGlyph::font`），渲染器据此从正确的图集条目光栅化；`xt_text_scale`
  把字面映射到显示的缩放系数。
- `xt_text_measure_width` 用 HarfBuzz 对 run 整形（因此字距调整与连字会生效），
  `xt_text_metrics` 返回 FreeType 的 ascent / descent / 正常行高。当找不到
  字体文件时，模块回退到确定性的逐字节近似，因此布局仍可工作。
- `xt_text_shape_run` 返回定位后的字形，`xt_text_rasterize` 渲染 8 位位图；
  渲染器把它们打包进图集。在 HiDPI 显示器上，字形以
  `font_size * SDL_GetWindowPixelDensity` 光栅化，而四边形以逻辑像素定位，
  因此文本保持清晰。
- 布局使用这些真实度量进行文本宽度、断行与 `line-height: normal` 计算；
  `measureText`/`fontMetrics` 把它们暴露给测试。

