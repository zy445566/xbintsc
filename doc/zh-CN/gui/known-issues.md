# 已知问题与待决问题

> 语言 / Language：[English](../../gui/known-issues.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

## 已知问题

- **WebGL 是 Vulkan SPIR-V 上的 WebGL 1.0 子集。** `canvas.getContext("webgl")`
  返回真正的 GPU 上下文（见 [WebGL](./webgl.md)）：GLSL ES 着色器由内置的
  glslang 翻译为 SPIR-V，绘图缓冲是一张离屏颜色纹理，页面直接合成而无须读回。
  WebGL 2、顶点数组对象、实例化、立方体贴图、浮点/深度纹理、多渲染目标、MSAA
  与模板操作尚未实现，`clear` 会忽略 scissor 矩形，`getContext("webgl2")` 仍返回
  `null`。在无法消费 SPIR-V 的 SDL_GPU 后端（Metal、Direct3D 12）上上下文也为
  `null`。`.js` 资源加载器仍保持 Three.js 可用：它的 `WebGLRenderer` 现在会探测
  上下文，有上下文时使用 Three.js 自己的渲染器，只有没有上下文时才回退到内置的
  Canvas2D 场景渲染器（`runtime/ext_gui/three_compat.js`）。较新的 Three.js 版本
  要求 WebGL 2，因此在 WebGL 2 落地前请固定使用 WebGL 1 的 Three.js 构建
  （或接受 Canvas2D 路径）。

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
- **CSS 覆盖范围是有意为之的子集。** 已实现 Grid、`clamp()`/`min()`/`max()`/`env()`、
  `var()` 自定义属性、`inset` 简写、`box-sizing`、`visibility`/`pointer-events`
  以及线性渐变背景。仍未实现：伪元素（`::before`/`::after`）、`clip-path`、
  `filter`/`backdrop-filter`、`mix-blend-mode`、`text-shadow`、径向/多层背景，
  以及 `@media` 块（解析器会跳过 at-rule）。这些属性会被忽略而不是近似实现，
  因此装饰效果可能缺失，但布局与文字仍然正确。Canvas 纹理使用最近邻采样，
  多层 `background` 只绘制一层基础线性渐变（18 条纯色带），不是完整的合成栈。
- **SVG 是 NanoSVG 的子集。** SVG *文件*（`<img src="logo.svg">`）与内联
  `<svg>` 子树共用 vendored 的 NanoSVG 解析/光栅化器（`runtime/ext_gui/svg.cpp`、
  `inline_svg.cpp`），它会渲染 `path`/`rect`/`circle`/`ellipse`/`line`/
  `polyline`/`polygon`、组、`defs` 渐变以及基于 `class` 的 `<style>` 规则。
  `<text>`、`<use>`、滤镜、遮罩与裁剪路径不会渲染，文档层叠的 CSS 也不会进入
  图像内部（这里的 `fill`/`stroke` 是 SVG 表现属性，不是 CSS 属性）。需要这些
  能力的矢量美术必须预先光栅化、用盒子绘制，或限制在受支持的语法内。参见
  [媒体](./media.md#图像m6a)。

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
  LLVM）构建它、链接一个 `gui,media` 程序，并随 x64 发布归档一起发布。示例在
  托管 runner 上的*运行*仍为试验性，因为那里没有 GPU/Vulkan 驱动（SDL_GPU 在
  Windows 上选择 **Vulkan** 后端，因此不需要 DXIL/`dxc` 工具链）。Windows
  arm64 仍跳过两个归档。在开发者机器上构建并运行
  `examples/gui/pelican-svg` 与 `examples/gui/pelican-bike` 是可以的。
- **Windows：x64 在 CI 中构建，arm64 与运行时验证仍为试验性。**
  `scripts/build-media.ts` 用 FFmpeg 的 MSVC 工具链
  （`--toolchain=msvc --cc=clang-cl`）生成 `media.lib`，其 COFF 对象与静态 CRT
  和 `gui.lib` 一致，`--ext media` 在 Windows 上可以正常链接并播放。构建它需要
  POSIX shell 运行 FFmpeg 的 `configure` 以及 GNU make；shell 来自 Git for
  Windows（克隆 SDL3 本来就需要 Git），make 是原生 GNU make（Chocolatey 的
  `make`，或 `xbintsc_MAKE`）。CI 会显式安装 make，在
  Windows x64 上使用与本地构建相同的 Visual Studio `vcvarsall` + 自带 LLVM
  构建 `gui.lib` 与 `media.lib`，链接 `gui,media` 程序，并把两个归档放进
  Windows x64 发布包；示例的*运行*仍因托管 runner 没有 GPU/Vulkan 驱动而保持
  试验性。Windows arm64 仍跳过两个归档；`<video controls>` 同样推迟——预期模式
  是 TS + CSS 叠层，如 `examples/gui/media.ts`。
- **D3D12 着色器：** SPIR-V（Vulkan）与 DXIL（Direct3D 12）二进制块均已构建并
  内嵌，非 Metal 路径不再跳过几何绘制。但在 **SDL 3.2.10** 上 DXIL 仍不可用：
  其 D3D12 后端会拒绝任何着色器中声明了 uniform buffer 的图形管线，而引擎正是
  用这种方式传视口。Windows 实际会选择 Vulkan，因此不受影响；若将来强制走
  D3D12，需在 SDL 升级后重新确认。