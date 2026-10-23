# 已知问题与待决问题

> 语言 / Language：[English](../../gui/known-issues.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

## 已知问题

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
  （COFF 对象 + `ar -M`/`llvm-ar`），但尚未在 CI 中验证，因此 Windows 步骤
  为试验性（job 保持绿色），`package` 会跳过它；在开发者机器上构建并运行
  `examples/gui/pelican-svg` 与 `examples/gui/pelican-bike` 是可以的。那里
  SDL_GPU 会选择 **Vulkan** 后端（`win.driver()` 返回 `"vulkan"`），因此归档
  不需要 DXIL/`dxc` 工具链；托管 Windows runner 没有 GPU/Vulkan 驱动，这才是
  CI 运行仍为试验性的原因。
- **Windows 上的媒体：** `media` 扩展目前仅支持 POSIX。GUI 引擎本身可在
  Windows 上构建，但没有该扩展时 `<video>`/`<audio>` 为空操作（打开失败并触发
  `error`），`--ext media` 会给出可操作的错误而不是继续链接。启用它需要
  MSVC 兼容的 FFmpeg 构建，推迟到 Windows GUI 路径本身验证之后。
  `<video controls>` 同样推迟——预期模式是 TS + CSS 叠层，如
  `examples/gui/media.ts`。
- **D3D12 着色器：** SPIR-V（Vulkan）与 DXIL（Direct3D 12）二进制块均已构建并
  内嵌，非 Metal 路径不再跳过几何绘制。但在 **SDL 3.2.10** 上 DXIL 仍不可用：
  其 D3D12 后端会拒绝任何着色器中声明了 uniform buffer 的图形管线，而引擎正是
  用这种方式传视口。Windows 实际会选择 Vulkan，因此不受影响；若将来强制走
  D3D12，需在 SDL 升级后重新确认。
