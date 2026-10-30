# 绘制、GPU 与文本

> 语言 / Language：[English](../../gui/rendering.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

## 绘制与显示列表（M4a/M4b）

`runtime/ext_gui/paint.{h,cpp}` 按画家顺序遍历布局树，生成一个与后端无关的
`DisplayList`：一个按画家顺序排列的 `ops` 列表，包含**矩形**（背景与四条实心
边框，带 `border-radius`）、**文本 run**（每个携带其文本、颜色与已解析的
`FontSpec`）与**图像**，背后由并列的负载数组支撑。每个 op 还携带当前生效的裁剪
矩形（最近的 `overflow: hidden` 祖先的 padding box）以及最多两个指向显示列表
三角化 `clip-path` 多边形的下标，因此 `overflow`/`clip-path` 裁剪与 `z-index`
顺序都能保留到渲染器。`DisplayList::dump()` 为 `paintList()`/`paintCount()`
提供数据。

`opacity` 会乘到该盒子及其后代绘制的每个颜色 alpha 上（沿绘制链逐层相乘），
因此小数 opacity 以及 CSS 解析器接受的 `opacity` 过渡都能看到效果。这是按图形
逐个近似的，而不是离屏分组：半透明盒子的重叠子元素会各自合成，浏览器则先把整组
合成一次再应用 alpha。`opacity: 0` 仍会跳过整棵子树。把隐藏态写成
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
  构建但在上游修复前不可用。Windows 不受影响：SDL_GPU 在那里会选择 Vulkan 后端
  （由 `win.driver()` 确认），使用 SPIR-V 二进制块。
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

### 背景、生成内容与特效（M19）

`background-color` 与每一条 `background-image` 层都会绘制。逗号分隔的层列表按
从底到上的顺序绘制（声明中的第一层在最上面），两种渐变均受支持：

- **线性**渐变在 18 个位置采样（重复渐变 64 个）并输出为纯色矩形；支持
  `to <side>`、`deg`/`grad`/`rad`/`turn`、百分比或长度（`px`/`em`/…）偏移、
  `color <start> <end>` 简写与多色标（色带沿最近轴行走，因此斜角会退化为
  最近的轴向）；
- **径向**渐变由 `renderer_gradient.cpp` 逐像素解算成一张 RGBA 纹理，作为单个
  图像四边形绘制（`PaintGradient`）。若沿用嵌套色带，由于每条色带都会覆盖整个
  内部区域，alpha 会叠加多次——淡出到 `transparent` 的暗角会把整个场景涂成实心
  黑盘。支持形状/尺寸关键字（`circle`/`ellipse`、`closest-side`/`farthest-side`/
  `closest-corner`/`farthest-corner`、显式半径）与 `at <position>`；关键字尺寸
  的椭圆保持盒子的宽高比并缩放到刚好接触对应边或角，与 CSS 一致。色标在预乘
  alpha 空间中插值，最后一个色标会延伸覆盖结束形状之外的剩余盒子。

`repeating-linear-gradient()` 与 `repeating-radial-gradient()` 会在整个盒子上
从 0 平铺到最后一个色标的偏移量；色标列表不会被强制结束在 100%，因此
`red 0%, blue 10%` 每 10% 重复一次。普通渐变的最后一个色标若小于 100%，则剩余
区域保持该颜色，与 CSS 一致。

背景色位于所有层下方。若渐变色标带透明度，渐变也会半透明；径向渐变逐像素精确，
线性渐变仍采样 18 条色带，较大的柔和渐变可能出现条带。

**生成内容**不需要新的显示列表类型：`::before`/`::after` 是 `style.cpp` 中
每次重算样式时重建的普通布局盒，因此其背景、边框、文本与 transform 都走同一条
绘制路径。`content` 支持字符串、`attr()` 与 open/close quote。

**`text-shadow`** 会作为阴影 `PaintText` run 排在真实 run 之前，因此文本管线
无需新 shader 就能绘制。带模糊的阴影是围绕偏移量展开的九个半透明副本（与
`filter: blur()` 相同的近似）。

**`clip-path`** 在 `paint_clip.cpp` 中解析为设备空间多边形（每个点都应用盒子的
transform），用耳切法三角化，并存入 `DisplayList::clip_paths`。每个 op 的
`PaintClip` 最多携带两个路径下标；`renderer_clip.cpp` 在上传时把每个凸的图形/
字形/图像四边形与这些三角形求交。即使 `polygon()` 是凹多边形也正确，无需模板或
shader 改动，但边缘是硬边（没有解析抗锯齿）。支持两层嵌套路径求交；更深时保留
最外两层。

**`filter`** 的颜色函数（`brightness`、`contrast`、`grayscale`、`sepia`、
`invert`、`saturate`、`hue-rotate`、`opacity`）会折进每个绘制颜色，因此盒子与
其后代都会看到滤镜。`blur()` 与 `drop-shadow()` 是显示列表层面的近似：模糊把每个
图形展开成九个半透明副本，投影用盒子/文字轮廓。这样普通绘制保持单渲染通道，代价
是组语义：被模糊/滤镜的组内重叠后代可能加深；`backdrop-filter` 无法采样元素背后
的内容，因此会解析并参与层叠，但不绘制。

**`mix-blend-mode`** 是唯一真正需要离屏图层的例外。非 `normal` 的盒子会被
`LayerBegin`/`LayerEnd` 标记包住（在 `paintList()` 中显示为 `layer blend=…` /
`end-layer`），渲染器把该组绘制到全视口离屏纹理，再用一个小型 GLSL 混合
shader 合成到场景上。全部十六种 CSS 模式（`multiply`、`screen`、`overlay`、
`darken`、`lighten`、`color-dodge`、`color-burn`、`hard-light`、`soft-light`、
`difference`、`exclusion`、`hue`、`saturation`、`color`、`luminosity`）都按
合成规范公式实现。shader 在运行时由内置 glslang 编译（Vulkan 用 SPIR-V），
再由 SPIRV-Cross 交叉编译为 MSL（Metal）；每个嵌套层级的两张场景纹理跨帧复用，
并通过 ping-pong 让背景在写入时仍可采样。既不吃 SPIR-V 也不吃 MSL 的后端
（这里的 D3D12/DXIL 路径没有 shader 编译器）仍用普通 source-over 混合绘制该组。
`backdrop-filter` 依旧不绘制。

## 文本技术栈（M4b）

`runtime/ext_gui/text.{h,cpp}` 是前端，通过一个小型后端表分发。默认后端是内置的
**stb_truetype**（`text_stb.cpp`、`runtime/ext_gui/stb_truetype.h`）。可选的
**`shaping`** 链接特性会安装 `text_hb.cpp`（HarfBuzz + FreeType 后端），非 ASCII
文本会自动选中它。

- 字体从常见系统路径解析（macOS 上的 Helvetica/Arial、Linux 上的
  DejaVu/Liberation、Windows 上的 Segoe UI/Arial），可用 `XT_GUI_FONT`
  （以及 `XT_GUI_FONT_MONO`）覆盖，并按 `(字体类别, 字号)` 缓存。目前只使用
  常规直立字面；字重/斜体选择是后续细化。
- 默认候选优先选择 **CJK 覆盖的 TrueType/TTC** 字面（Windows 的 Microsoft
  YaHei、macOS 的 PingFang/STHeiti、Linux 的 Noto Sans CJK/WenQuanYi），
  再是纯拉丁字面。stb_truetype 无法解析的候选（例如 CFF/OTF 集合）会被跳过，
  继续尝试下一个。
- **主字面缺少的码点会回退到其他字面。** 引擎维护一条小的回退链——先平台的
  符号字体（Segoe UI Symbol、Apple Symbols、Noto Sans Symbols、DejaVu Sans），
  再是另一字体类别——并为每个码点解析出回退链中第一个拥有该字形的字面。
  run 以每个字面一段的方式整形，因此混合 run（拉丁文加箭头、制表符或几何图形，
  或 CJK 中的拉丁文）会绘制每个字形，而不是把缺失的字形当作 `.notdef` 丢弃。
  每个整形后的字形携带自己的字面（`ShapedGlyph::font`），渲染器据此从正确的
  图集条目光栅化；`xt_text_scale` 把字面映射到显示的缩放系数。
- `xt_text_measure_width` 把每个码点映射为字形并累加 advance 与字体的 `kern`
  表；`xt_text_metrics` 返回 stb_truetype 的 ascent / descent / 正常行高。
  当找不到字体文件时，模块回退到确定性的逐字节近似，因此布局仍可工作。
- `xt_text_shape_run` 返回定位后的字形，`xt_text_rasterize` 渲染 8 位位图；
  渲染器把它们打包进图集。在 HiDPI 显示器上，字形以
  `font_size * SDL_GetWindowPixelDensity` 光栅化，而四边形以逻辑像素定位，
  因此文本保持清晰。
- 布局使用这些真实度量进行文本宽度、断行与 `line-height: normal` 计算；
  `measureText`/`fontMetrics` 把它们暴露给测试。

整形取舍：默认是简单整形器，不是完整的 OpenType 引擎。它覆盖拉丁/CJK/西里尔/
希腊文、基础字距与逐码点回退，但不执行连字或复杂文种（阿拉伯/印度/泰文）整形，
且 stb_truetype 只接受 TrueType 轮廓，不支持 CFF/OTF 或可变字体。该选择让
DOM-only 二进制约 3 MiB。可选的 **`shaping`** 链接特性会安装 HarfBuzz + FreeType
后端（约 1.2 MiB），恢复完整 OpenType 整形与 CFF/OTF/可变字体支持；非 ASCII 文本
会自动选中，也可用 `--feature shaping` 强制。

## 性能（M20）

引擎是保留模式管线：一次 DOM 变更会重算样式、重新布局、重建显示列表并上传
顶点数组；没有任何变化的帧不产生 CPU 或 GPU 工作。为了让上千个元素的文档保持
可交互，每个阶段都有预算，并且都可以测量。设置 `XT_GUI_PERF=1` 会逐帧打印一行
（`XT_GUI_PERF=<N>` 则每 N 帧聚合一行）；`win.stats()` 返回同样的计数器，
`tests/e2e/gui-perf.test.ts` 断言的正是这些计数。

- **层叠。** 每次重算样式时，`@media` 前导条件**每条规则只求值一次**（过去是
  每条规则 × 每个节点各解析一次，这是大文档上最大的一笔开销）。样式表里没有
  `::before`/`::after` 规则时完全跳过生成内容；声明候选列表在深度优先遍历中
  复用而不是每个元素分配一次；只有样式表真的包含 `:hover`/`:focus` 规则时，
  这两个状态变化才会触发重算。
- **文本。** 布局与绘制共享一个有界的整形缓存（`text_cache.cpp`）：
  `(face, text) -> 字形 + 前进宽度`。它使用两代、每代最多 2 MiB 字形 / 8,192 条
  条目；某一代写满时丢弃**备用**代并切换过去，因此热路径上没有 LRU 记账。
  一个词只整形一次，之后每次布局和重绘都复用它。主字体查找也按
  （字体族类别，字号）缓存。
- **空写不生效。** 当节点已经显示该值时，`textContent` 与 `style.<属性>` 不做任何
  事：文档不会被标脏，也就没有重算样式、重新布局、重绘或上传。因此每帧都写入相同
  计时文本、相同血条宽度或相同 transform 的 HUD 不产生任何开销 —— 在此之前，每次
  赋值都会重建文本节点（一次分配，收集器随后还要清扫）或重写整条内联样式，并把
  文档标脏触发全文重算。`classList` 与 `removeAttribute` 早就是这样；`setAttribute`
  仍会无条件标脏。
- **帧循环。** 只有文档发生变化（或首帧）时才把媒体/画布表与 DOM 对齐，不再每帧
  遍历整棵树。只有确实发生了事情才提交呈现（present）——DOM 变更、滚动、动画帧、
  过渡、新的视频/画布帧、expose/resize 事件——因此静态页面完全不获取 swapchain、
  也不绘制。expose 是唯一由窗口系统（而非页面）要求的呈现，`exposes` 统计这些帧，
  因此即使窗口服务器很晚才映射窗口，`presents - exposes - deferred` 仍然只是
  文档自身的工作量。取不到交换链图像的帧（GPU 仍持有图像时 SDL 的非阻塞获取会跳过
  该帧，最小化的窗口则完全没有图像）不会丢弃这次失效，而是保留到下一帧再呈现，
  这类呈现计为 `deferred`。调试"漏掉的失效"时可用 `XT_GUI_ALWAYS_PRESENT=1`
  恢复旧的每帧提交。
- **绘制与上传。** 显示列表和顶点暂存数组挂在窗口中按帧清理，容量得以保留。
  一个脏帧的所有顶点/图集上传共用一个命令缓冲区（一次提交），几何体的暂存
  transfer buffer 被复用（上一帧的拷贝可能仍在飞行中，因此按 cycle 方式映射），
  没有任何子元素带 z-index 时绘制会跳过排序和临时数组。
- **绘制调用。** 相邻且 pipeline、裁剪和纹理相同的 op 仍会合并为一个 run，
  渲染器现在只在 pipeline / 顶点缓冲 / 裁剪矩形真正变化时才重新绑定。
  `win.stats()` 里的 `drawCalls`/`drawMs` 报告 run 数量与其开销，它们是后续工作
  （感知重叠的合并）的度量依据——在它落地之前，画家顺序要求背景与文本交替的
  run 必须保持独立。
- **立即模式绘制表面。** `<canvas>` 2D 与 WebGL 由脚本驱动，开销按调用次数计
  而不是按文档变更计；上面的计数器只覆盖保留模式管线。
  `tests/e2e/gui-draw-perf.test.ts` 测量它们：在参考的 Windows x64/Vulkan 机器上，
  一帧 1280×720、包含 5,000 个纯色 `fillRect`、200 条 8 点折线、300 次
  `fillText` 和 200 次 1:1 `drawImage` 的场景从 231ms 降到 158ms（填充 4.3ms、
  描边 151ms、文本 1.4ms、图像 1.6ms）；2,000 次带逐次矩阵的 WebGL
  `drawElements` 从 38.6ms 降到 27.5ms，同时每帧打开的渲染 pass 从 2,001 次
  降到 1 次，向 4 MiB 缓冲写入 64 KiB 的 `bufferSubData` 不再重传整个缓冲。
  100×100 的画布局部重绘现在只上传 40 KB，而不是整张 1.2 MB 位图。测试断言的是
  计数器（每帧一次上传、稳定状态不新建管线、只传脏字节），因此墙钟数字仅供
  参考，CI 上的软件 GPU 不会被误判。

已知限制：一次变更仍会对**整个**文档重算样式并重新布局（没有增量子树失效），
只影响绘制的变更（例如 hover 颜色）目前同样会重排；后续工作应以 `win.stats()`
的数据为依据。元素自带的 `style="…"` 内联属性在该元素每次重算样式时都会重新解析。

作为参照，`tests/e2e/gui-perf.test.ts` 会构建一个 200 行的列表（约 1,000 个元素、
400 个绘制 run）并打印实测数字，例如在 Windows x64/Vulkan 机器上修改一个
`textContent` 字段得到
`textFrame=5.2ms restyle=3.6ms layout=1.7ms paint=1.0ms upload=0.8ms draw=0.4ms`。
绝对值因机器而异；测试断言的是**计数器**（一次重算、一次布局、命中缓存而不是
重新整形、空闲帧不做任何工作），回归时变化的正是这些。

