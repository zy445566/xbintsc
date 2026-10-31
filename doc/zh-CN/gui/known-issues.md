# 已知问题与待决问题

> 语言 / Language：[English](../../gui/known-issues.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

## 已知问题

- **WebGL 1.0 与 2.0 运行在 GPU 上，但只实现了各自的子集。**
  `canvas.getContext("webgl")` / `"webgl2"` 返回真正的 GPU 上下文（见
  [WebGL](./webgl.md)）：GLSL ES 着色器由内置 glslang 翻译为 SPIR-V——Vulkan
  后端直接消费，Metal 上再由内置 SPIRV-Cross 交叉编译为 MSL——绘图缓冲
  是一张离屏颜色纹理，页面直接合成而无须读回。WebGL 2 增加了 VAO、实例化、
  整数属性、采样器对象、`texStorage2D`、缓冲拷贝、Uniform 块、立方体/3D/数组
  纹理、浮点/深度/压缩纹理、多渲染目标与 scissor 清除。`blitFramebuffer` 复制
  颜色与深度；transform feedback、MSAA 与模板 blit 尚未实现（SDL_GPU 没有
  transform feedback 阶段，其 Metal 多采样 resolve 不可靠，模板附件也无法采样）。
  在两种格式都不接受的 SDL_GPU 后端（Direct3D 12，需要 DXIL）上两种上下文都为
  `null`。`.js` 资源加载器现在只修补 three.js 的着色器循环展开；
  `WebGLRenderer` 直接运行在真实后端上，不再有 Canvas2D 回退。阴影贴图
  （比较型 `sampler2DShadow` 采样深度纹理）、立方体贴图/环境贴图与 HDR 渲染目标
  均已支持。
- **部分 Intel Mac 上比较型采样器会被错误编译。** 在 Intel GPU（在 Intel Iris
  Plus Graphics + macOS 26 上观察到）上，Metal 着色器编译器会丢弃调用
  `sample_compare` 的片元函数：绘制不产生任何像素，于是
  `texture(sampler2DShadow, …)` 读到黑色，阴影贴图消失。MSL 本身是正确的
  （`depth2d<float>` + `sample_compare`），采样器也带比较操作，而同一个程序在
  打开 Metal GPU 着色器校验后渲染正常——在这类机器上以
  `MTL_SHADER_VALIDATION=1` 运行即可让
  `tests/e2e/gui-webgl2-shadow-sampler.test.ts` 通过。这指向驱动的着色器编译器
  而非引擎，因此以环境变量作为绕过办法；其他 Apple GPU 未必存在该问题。


- **自动关闭时的偶发崩溃。** 设置 `XT_GUI_AUTOCLOSE_MS` 结束运行时，约五次中有两次会以原生
  访问违例（`0xC0000005`）退出，其余运行与正常退出无异。该问题只在**动画帧循环运行中**复现
  （静态文档可以正常关闭），且**不是示例游戏自身**的代码：没有任何异常信息，崩溃多发生在
  启动后一秒内、与截止时间无关，而提高 `XT_GC_THRESHOLD` 可以掩盖它，因此看起来是
  GC/析构竞态。交互式运行（不设自动关闭）可稳定运行数分钟。此前的一个已知成因是引擎在标记-清扫
  收集器看不见的 C++ 容器里缓存 `xt_value`（AOT `<script>` 注册表、元素句柄、元素监听器、
  排队/正在运行的 `requestAnimationFrame` 回调），现在它们由 GC 根提供者标记
  （`xt_gui_script_gc_scan` 与 `xt_gui_gc_scan_roots`），该类 use-after-free 已消除。
  在 M12 的节点生命周期改动之后它已完全无法复现：对
  `examples/gui/pelican-bike` 做了 44 次自动关闭运行（截止 800 ms–4 s，分别使用默认、
  1 MiB 与 64 MiB 的收集阈值，而旧记录称约五次两次崩溃），全部正常退出，指向与内存问题
  相同的根因——分离节点池与句柄表随帧数增长，同时收集器阈值只升不降。此处仍保留该条目，
  因为该归档按平台构建且故障与时机相关。`tests/e2e/gui-example.test.ts`
  因此把该退出码视为可接受结果，行为断言放在不依赖 GPU 的
  `tests/e2e/gui-example-browser.test.ts` 中。
- **`display: none -> flex` 的重排。** 通过类名切换让隐藏元素显示在 flex 容器上时，也曾观察
  到同样的崩溃；`examples/gui/pelican-bike` 改用 `opacity` 显示游戏结束遮罩。
- **CSS 覆盖范围是有意为之的子集。** 已实现 Grid、`calc()`（含嵌套算术及
  `min()`/`max()`/`clamp()` 内的 `calc()`）、`clamp()`/`min()`/`max()`/`env()`、
  `var()` 自定义属性、`inset` 简写、`box-sizing`、`visibility`/`pointer-events`、
  `@media` 查询、生成内容（`::before`/`::after`）、线性/径向多层背景、
  `text-shadow`（真正的高斯）、`box-shadow`（外阴影/内阴影、spread、多层）、
  `clip-path`（`inset`/`circle`/`ellipse`/`polygon`）、`filter` 颜色函数、
  `@keyframes` 动画与完整的 `transition` 时间函数。仍有限制：`background-clip`
  与 `background-position`/`size`/`repeat` 未实现；组 `opacity` 是按图形 alpha
  近似而非真正的组合成；D3D12/DXIL 后端没有混合与 `backdrop-filter` 合成器，
  那里会退回普通 source-over 绘制并跳过背景滤镜。`filter: blur()` 仍是按图形做
  九次偏移的近似，而不是真正的组模糊，重叠的子元素会加深；`drop-shadow` 用盒子
  轮廓近似；最多只交叉两层嵌套 `clip-path`（更深时保留外侧两层）；
  图像/画布除图像着色器能携带的亮度与透明度染色外不做颜色滤镜。Canvas 纹理使用
  最近邻采样。背景线性渐变仍是纯色带（18 条，重复渐变 64 条），较大的柔和渐变
  可能出现轻微条带；径向渐变则逐像素光栅化成纹理（每边最大 1024 px，超出后拉伸，
  因此超大盒子上的硬色标会略微变软）。
- **SVG 是 NanoSVG 的子集。** SVG *文件*（`<img src="logo.svg">`）与内联
  `<svg>` 子树共用 vendored 的 NanoSVG 解析/光栅化器（`runtime/ext_gui/svg.cpp`、
  `inline_svg.cpp`），它会渲染 `path`/`rect`/`circle`/`ellipse`/`line`/
  `polyline`/`polygon`、组、`defs` 渐变以及基于 `class` 的 `<style>` 规则。
  `<text>`、`<use>`、滤镜、遮罩与裁剪路径不会渲染，文档层叠的 CSS 也不会进入
  图像内部（这里的 `fill`/`stroke` 是 SVG 表现属性，不是 CSS 属性）。因此只靠滤镜
  出图的 SVG（`feTurbulence` 噪声、模糊等）会显示未经过滤的形状——常见情况是一个
  纯色矩形——而不是滤镜结果。需要这些能力的矢量美术必须预先光栅化、用盒子绘制，
  或限制在受支持的语法内。参见[媒体](./media.md#图像m6a)。
- **每次变更仍是整文档重算。** 一次 DOM 变更会重新计算层叠并重新布局整个文档
  （没有增量子树失效），只影响绘制的变更（例如 hover 颜色）同样会重排。带生成
  内容（`::before`/`::after`）的文档没有特殊处理。各阶段的实测开销由
  `win.stats()`/`XT_GUI_PERF` 暴露，后续优化以它为依据；参见
  [性能](./rendering.md#性能m20)。
- **绘制调用遵循画家顺序。** 只有当 run 在画家顺序上相邻且 pipeline、裁剪与
  纹理都相同时才会合并，因此"背景与文本逐行交替"的列表每行要付一个图形 run 和
  一个文本 run（见 `win.stats()` 的 `drawCalls`）。要减少绘制调用需要感知重叠的
  合并器（文本可能被后续背景覆盖），目前未实现。实测：200 行基准页发出 400 次
  绘制调用，绘制阶段约耗时 0.4 ms，因此它是"绘制调用占主导"的页面的后续优化项，
  而不是当前的瓶颈。
- **文本整形缓存有界。** 两代、每代最多 2 MiB 字形 / 8,192 条条目保存
  `(face, text) -> 字形`。不同单词数超过上限的文档在缓存轮换后会重新整形一部分；
  `win.stats()` 的 `textCacheBytes`/`textShapeHits` 能看出它是否成为瓶颈。
  元素自带的 `style="…"` 内联属性在该元素每次重算样式时都会重新解析。

- **文本整形是简单整形器。** stb_truetype 覆盖拉丁/CJK/西里尔/希腊文、基础
  `kern` 与逐码点回退，但不执行 OpenType 连字或复杂文种（阿拉伯/印度/泰文）整形，
  且只接受 TrueType 轮廓（不支持 CFF/OTF 或可变字体）；彩色 emoji 也不会以彩色
  渲染。这是 DOM-only 二进制约 3 MiB 的代价；可选的 `shaping` 特性会重新链接
  HarfBuzz + FreeType，恢复完整整形。
- **链接特性检测是静态的。** `media`/`gpu`/`images`/`shaping`/`webgl` 从打包后的 HTML/CSS/TS（包括导入的
  `.html` 与其内联 CSS）里选择。用运行时数据（`fetch`、`readFileSync`、函数返回值）
  拼 HTML/CSS 的页面无法被看到；扫描器在能证明 sink 是动态时会保守地启用可选特性，
  但对于它无法分类的不透明值，需要显式 `--feature media`/`gpu`/`images`/`shaping`/`webgl`（或 `all`）。
  `--verbose` 会打印选中的集合。见[链接特性](./build.md#链接特性)。

## 待决问题

- Linux 首发是只发布 X11、Wayland，还是两者都发布。（决定：先 X11，
  Wayland 随后。）
- **Windows：链接命令行里不能点名 C++ 运行时。** `gui.lib` 用*静态* CRT 构建
  （`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`），引擎每个目标文件都把 `libcpmt`
  记为默认依赖库；这既是 clang 在 Windows 上的默认，也是 xbintsc 运行时对象所用
  的运行时（见 `scripts/build-gui.ts`）。如果再额外要求 DLL 版运行时——gui 扩展
  过去会传 `-lmsvcprt`——同一批符号的两份定义就会冲突：`LNK2038: mismatch detected
  for 'RuntimeLibrary'`，外加满屏 `LNK2005: already defined`，最终表现为
  `clang: linker command failed with exit code 1169`。现在
  `src/extensions/gui/index.ts` 只列系统库（`user32`、`gdi32`、`d3d12` 等）。
  Windows 链接不再需要手动准备 MSVC 环境：`scripts/build-gui.ts` 会解析与驱动
  相同的、面向 MSVC 的 clang（`PATH` 上没有更新版本时就是 Visual Studio 自带的
  LLVM）。
- Windows：`scripts/build-gui.ts` 会生成 MSVC 兼容的 `gui.lib`
  （COFF 对象 + `ar -M`/`llvm-ar`）。Windows x64 的 CI job 使用 Visual Studio
  工具链（`.github/actions/vs-toolchain` 导入 `vcvarsall` 并采用 VS 自带的
  LLVM）构建它、链接一个会触发 `media` 链接特性的 `gui` 程序，并随 x64 发布归档
  一起发布。示例在
  托管 runner 上的*运行*仍为试验性，因为那里没有 GPU/Vulkan 驱动（SDL_GPU 在
  Windows 上选择 **Vulkan** 后端，因此不需要 DXIL/`dxc` 工具链）。Windows
  arm64 仍跳过两个归档。在开发者机器上构建并运行
  `examples/gui/pelican-svg` 与 `examples/gui/pelican-bike` 是可以的。
- **Windows：x64 在 CI 中构建，arm64 与运行时验证仍为试验性。**
  `scripts/build-media.ts` 用 FFmpeg 的 MSVC 工具链
  （`--toolchain=msvc --cc=clang-cl`）生成 `media.lib`，其 COFF 对象与静态 CRT
  和 `gui.lib` 一致，自动选中的 `media` 特性在 Windows 上可以正常链接并播放。构建它需要
  POSIX shell 运行 FFmpeg 的 `configure` 以及 GNU make；shell 来自 Git for
  Windows（克隆 SDL3 本来就需要 Git），make 是原生 GNU make（Chocolatey 的
  `make`，或 `xbintsc_MAKE`）。CI 会显式安装 make，在
  Windows x64 上使用与本地构建相同的 Visual Studio `vcvarsall` + 自带 LLVM
  构建 `gui.lib` 与 `media.lib`，链接一个会触发该特性的 `gui` 程序，并把两个归档放进
  Windows x64 发布包；示例的*运行*仍因托管 runner 没有 GPU/Vulkan 驱动而保持
  试验性。Windows arm64 仍跳过两个归档；`<video controls>` 同样推迟——预期模式
  是 TS + CSS 叠层，如 `examples/gui/media.ts`。
- **D3D12 着色器：** SPIR-V（Vulkan）与 DXIL（Direct3D 12）二进制块均已构建并
  内嵌，非 Metal 路径不再跳过几何绘制。但在 **SDL 3.2.10** 上 DXIL 仍不可用：
  其 D3D12 后端会拒绝任何着色器中声明了 uniform buffer 的图形管线，而引擎正是
  用这种方式传视口。Windows 实际会选择 Vulkan，因此不受影响；若将来强制走
  D3D12，需在 SDL 升级后重新确认。