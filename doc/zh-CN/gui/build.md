# 打包与构建

> 语言 / Language：[English](../../gui/build.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

- 源码位于 `runtime/ext_gui/`（C/C++），外加 `vendor/` 下的 vendored 库
  （被 gitignore；按需获取）。
- `npm run gui`（`scripts/build-gui.ts`）获取固定版本的 SDL3（`SDL3_TAG`，
  默认 `release-3.2.10`），构建静态 SDL3，获取并构建静态 FreeType
  （`FREETYPE_VERSION`）与 HarfBuzz（`HARFBUZZ_VERSION`），编译引擎，并把
  所有内容合并到 `runtime/lib/<os>-<arch>/gui.a`（Windows 上 MSVC ABI 则为
  `gui.lib`），与 `core.a` 采用相同约定，静态打包三者。它还会获取、配置并构建
  一个最小化的静态 **FFmpeg**（`FFMPEG_VERSION`，默认 `7.1`），只启用媒体元素
  所需的 demuxer/parser/decoder，并把那些归档合并进 `gui.a`，同时为引擎定义
  `XT_GUI_WITH_FFMPEG`。合并使用 macOS 上的
  `libtool` 与其他平台上的 `ar -M`（GNU/LLVM）；CMake 归档会在构建根目录或
  `Release/` 下按任一生成器风格被发现。
- `src/extensions/gui/index.ts` 通过 `nativeObjects()` 暴露该归档（经由
  `findRuntimeLibrary`，因此 `.a`/`.lib` 都可用），并通过 `linkerFlags()`
  暴露操作系统框架。
- CI 在组装发布归档之前构建 `gui.a`，因此它会随 `runtime/lib/<slug>/` 一起
  发布（发布 tarball 会复制整个 `runtime/` 树）。归档在 Linux（Xvfb 下，
  使用 lavapipe 软件 Vulkan 驱动）与 macOS 上构建并运行示例；Windows 为
  试验性。
- 现有的缓存指纹已经会对 `nativeObjects()` 的内容做哈希，因此重建 `gui.a`
  会自动使缓存二进制失效。
- CMake 构建树位于其源码**旁边**（`vendor/sdl-build`、`vendor/freetype-build`、
  `vendor/harfbuzz-build`），绝不放在源码内部：`CMakeCache.txt` 记录的是配置它
  的那台机器，而 SDL 的视频后端是 `option()`，因此先运行的那次配置会一直生效。
  所以一个同时携带 `build/` 的 `vendor/SDL` 缓存树，会把某台机器的
  `SDL_X11`/`SDL_WAYLAND` 选择带进另一台机器的配置——即使头文件已安装，SDL 也会
  以 "SDL could not find X11 or Wayland development libraries" 中止。CI 缓存按
  OS/arch 区分键值，且不包含构建树；`npm run gui` 还会在 Debian/Ubuntu 上提前
  检查 X11/Wayland 头文件，并按名称报告缺失的开发包（当头文件位于该检查未知的
  前缀下时，可设置 `xbintsc_SKIP_HEADER_CHECK=1`）。

## 运行示例

```sh
npm run runtime      # runtime 改动后重建 core.a
npm run gui          # 构建 runtime/lib/<os>-<arch>/gui.a（首次会获取 SDL3）
xbintsc run examples/gui/hello.ts --ext gui
```

设置 `XT_GUI_AUTOCLOSE_MS=<n>` 可在 `n` 毫秒后关闭所有窗口，e2e 测试
（`tests/e2e/gui-*.test.ts`）用它来无头运行。

这里有两个示例：`hello.ts`（HTML/CSS + 响应式计数器）与
[`pelican-bike/`](../../../examples/gui/pelican-bike)（一个可玩的 2D 游戏，使用分层
定位、CSS 像素精灵、`requestAnimationFrame` 与键盘输入）——操作方式与它输出的

`examples/gui/media.ts`（配合 `media.html`）演示 `<video>`/`<audio>`：它在
`<video>` 上叠一个 CSS 进度条，并用 `play()`/`pause()`/`currentTime`/
`volume` 驱动播放。媒体 `src` 相对于进程工作目录解析，因此从仓库根目录运行：
`xbintsc run examples/gui/media.ts --ext gui`。几何报告见该目录的 README。

这些测试套件在 Windows 上默认跳过（该平台会构建归档，但 CI 未验证运行）。
如果本机有 `gui.lib` 与可用的 GPU，可用 `xbintsc_GUI_TESTS=1` 选择启用：

```bat
set xbintsc_GUI_TESTS=1
npx vitest run tests/e2e/gui-layout.test.ts tests/e2e/gui-example.test.ts
```

在无头 Linux 机器上，安装 SDL3 构建头文件，并在 Xvfb 与软件 Vulkan 驱动下
运行：

```sh
sudo sh scripts/install-gui-deps.sh   # clang、cmake、X11/Wayland 头文件、Vulkan、Xvfb
npm run runtime && npm run gui
xvfb-run -a --server-args="-screen 0 1280x720x24" \
  npx tsx src/cli/main.ts run examples/gui/hello.ts --ext gui
```

包清单集中在 `scripts/install-gui-deps.sh`（`apt-get install -y clang cmake
libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev
libxfixes-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev
libasound2-dev libpulse-dev libdbus-1-dev libudev-dev libdrm-dev libgbm-dev
libgl1-mesa-dev libegl1-mesa-dev libvulkan-dev mesa-vulkan-drivers xvfb`），
这样每个构建 `gui.a` 的 Linux job 都安装同一套依赖：默认使用 X11 后端，
Wayland 也已启用但尚未实际演练。任何构建该归档的地方都要先装它——没有这些
头文件，SDL3 找不到窗口后端。

