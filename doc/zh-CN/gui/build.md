# 打包与构建

> 语言 / Language：[English](../../gui/build.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

- 源码位于 `runtime/ext_gui/`（C/C++），外加 `vendor/` 下的 vendored 库
  （被 gitignore；按需获取）。媒体解码位于 `runtime/ext_media/`，单独构建。
- `npm run gui`（`scripts/build-gui.ts`）获取固定版本的 SDL3（`SDL3_TAG`，
  默认 `release-3.2.10`），构建静态 SDL3，获取并构建静态 FreeType
  （`FREETYPE_VERSION`）与 HarfBuzz（`HARFBUZZ_VERSION`），编译引擎，并把
  所有内容合并到 `runtime/lib/<os>-<arch>/gui.a`（Windows 上 MSVC ABI 则为
  `gui.lib`），与 `core.a` 采用相同约定，静态打包三者。合并使用 macOS 上的
  `libtool` 与其他平台上的 `ar -M`（GNU/LLVM）；CMake 归档会在构建根目录或
  `Release/` 下按任一生成器风格被发现。
- `npm run media`（`scripts/build-media.ts`）构建可选的 **FFmpeg** 后端。它获取
  FFmpeg（`FFMPEG_VERSION`，默认 `7.1`），配置一个只启用媒体元素所需
  demuxer/parser/decoder 的最小静态构建，用它编译
  `runtime/ext_media/media.cpp`，并把所有内容合并到
  `runtime/lib/<os>-<arch>/media.a`（Windows 上 MSVC ABI 则为 `media.lib`）。
  `gui.a` **不会**链接 FFmpeg：程序通过 `--ext media` 显式启用播放能力。
- `src/extensions/gui/index.ts` 通过 `nativeObjects()` 暴露引擎归档（经由
  `findRuntimeLibrary`，因此 `.a`/`.lib` 都可用），并通过 `linkerFlags()`
  暴露操作系统框架。`src/extensions/media/index.ts` 对 `media.a`/`media.lib`
  做同样的暴露，并把注册桥（`runtime/ext_media/media_bridge.c`）作为普通
  runtime 源编译。
- `scripts/build-gui.ts` 使用与驱动**相同**的 clang
  （`src/driver/toolchain-provider.ts`）；Windows 上就是 Visual Studio 自带的、
  面向 MSVC 的 LLVM，并配套使用其同目录下的 `clang++`/`llvm-ar` 来配置 CMake、
  编译引擎和合并归档。脚本不提供 `gcc`/`g++` 回退，Windows 上也始终使用
  `llvm-ar`，因此归档不会混入其他目标文件格式或 C++ 运行时。Visual Studio
  工具链按绝对路径查找，无需开发者命令提示符。Windows 上 SDL_GPU 会选择 Vulkan
  后端，不需要 DXIL/`dxc` 工具链。
- `scripts/build-media.ts` 使用同一套已解析的工具链。Windows 上它走 FFmpeg
  自带的 MSVC 模式：`configure --toolchain=msvc --cc=clang-cl` 把静态 FFmpeg
  编译为使用静态 MSVC CRT 的 COFF 对象，`clang++` 编译播放器，`llvm-ar`
  把 COFF 归档合并成 `media.lib`。编译器链完全来自 Visual Studio
  （clang-cl、`cl.exe`、`lib.exe`、`llvm-ar`）。FFmpeg 的
  configure 是 POSIX shell 脚本、Makefile 是 GNU make 格式，因此仍需要这两个
  构建驱动：shell 来自 Git for Windows 自带的 bash（克隆 SDL3 本来就需要
  Git），make 是原生 GNU make（如 Chocolatey 的 `make`，或用 `xbintsc_MAKE`
  指定）。脚本会把 configure 写出的源码路径改写成盘符路径，并跳过依赖提取，
  因为原生 make 无法原样传递 MSVC `/showIncludes` 的 awk 管道。
  `cl.exe`/`lib.exe` 只被 configure 的工具链探测使用，由脚本从 Visual Studio
  安装目录解析，不需要开发者命令提示符。`xbintsc_SHELL` 与 `xbintsc_MAKE`
  可覆盖自动发现的 shell/make。头文件与库由脚本复制到
  `vendor/ffmpeg-build-msvc/dist`，而不是执行 `make install`。
- CI 在组装发布归档之前构建 `gui.a`/`gui.lib` 与 `media.a`/`media.lib`，因此它们
  会随 `runtime/lib/<slug>/` 一起发布（发布 tarball 会复制整个 `runtime/` 树）。
  归档在 Linux（Xvfb 下，使用 lavapipe 软件 Vulkan 驱动）与 macOS 上构建并运行
  示例；Windows x64 也会构建两个归档并链接一个 `gui,media` 程序，因此 MSVC
  构建在 CI 中得到验证，只是*运行*示例仍为试验性（托管 runner 没有 GPU/Vulkan
  驱动）。Windows arm64 仍跳过两个归档（FFmpeg 的 MSVC arm64 构建尚未验证）。
- Windows CI 通过 `.github/actions/vs-toolchain` →
  `scripts/setup-vs-toolchain.ps1` 使用与本地构建相同的 Visual Studio
  `vcvarsall` 环境和自带的 LLVM（不再单独安装 Chocolatey LLVM）。媒体构建时该
  action 还会通过 Chocolatey 安装 GNU make 并把 `xbintsc_MAKE` 指向它；FFmpeg
  源码与已安装的 `vendor/ffmpeg-build-msvc/dist` 前缀按脚本哈希缓存。
  configure/make 构建树本身不缓存（其中记录了另一台机器的绝对路径与编译选项）。
- 现有的缓存指纹已经会对 `nativeObjects()` 的内容与 `runtimeSources()` 的内容
  做哈希，因此重建任一归档都会自动使缓存二进制失效。
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
npm run media        # 可选：构建 media.a（首次会获取并构建 FFmpeg）
xbintsc run examples/gui/hello.ts --ext gui
```

Windows 上同样的命令在普通 PowerShell 中即可产出 `gui.lib` 与 `media.lib`，
不需要开发者命令提示符。Visual Studio 安装提供面向 MSVC 的 `clang++`、
`clang-cl`、`cl.exe` 与 `lib.exe`；`npm run media` 还需要 POSIX shell 与
GNU make 来运行 FFmpeg 的 `configure`：shell 来自 Git for Windows（克隆
SDL3 本来就需要 Git），GNU make 使用原生二进制——可 `choco install make`，
或用 `xbintsc_MAKE` 指定。脚本会指出缺失的工具，并支持
`xbintsc_SHELL` / `xbintsc_MAKE` 覆盖。

如果需要显式选择这套工具链（例如在全新终端中，或想与 CI 使用同一个编译器），
运行：

```powershell
pwsh -File scripts/setup-vs-toolchain.ps1
```

它会导入 `vcvarsall`（MSVC、Windows SDK、MSBuild），并把 `xbintsc_CLANG` 指向
该 Visual Studio 安装自带的 LLVM。GitHub Actions 通过
`.github/actions/vs-toolchain` 调用同一个脚本，因此 CI 不再单独安装
Chocolatey/独立 LLVM，CI、本地构建与预编译归档共用同一条编译链。

设置 `XT_GUI_AUTOCLOSE_MS=<n>` 可在 `n` 毫秒后关闭所有窗口，e2e 测试
（`tests/e2e/gui-*.test.ts`）用它来无头运行。

这里有两个示例：`hello.ts`（HTML/CSS + 响应式计数器）与
[`pelican-bike/`](../../../examples/gui/pelican-bike)（一个可玩的 2D 游戏，使用分层
定位、CSS 像素精灵、`requestAnimationFrame` 与键盘输入）——操作方式与它输出的

`examples/gui/media.ts`（配合 `media.html`）演示 `<video>`/`<audio>`：它在
`<video>` 上叠一个 CSS 进度条，并用 `play()`/`pause()`/`currentTime`/
`volume` 驱动播放。它需要单独的可选 `media` 扩展，因此要构建并同时启用两者。
媒体 `src` 相对于进程工作目录解析，因此从仓库根目录运行：
`xbintsc run examples/gui/media.ts --ext gui,media`。几何报告见该目录的 README。

这些测试套件在 Windows 上默认跳过（归档可以在本机通过 Vulkan 运行，但 CI
runner 没有可用的 GPU 驱动）。如果本机有 `gui.lib` 与可用的 GPU，可用
`xbintsc_GUI_TESTS=1` 选择启用；如果还运行过 `npm run media`，媒体套件也会在
Windows 上走真实的解码路径：

```bat
set xbintsc_GUI_TESTS=1
npx vitest run tests/e2e/gui-layout.test.ts tests/e2e/gui-example.test.ts
npx vitest run tests/e2e/gui-media.test.ts
```

在无头 Linux 机器上，安装 SDL3 构建头文件，并在 Xvfb 与软件 Vulkan 驱动下
运行：

```sh
sudo sh scripts/install-gui-deps.sh   # clang、cmake、X11/Wayland 头文件、Vulkan、Xvfb
npm run runtime && npm run gui
npm run media                         # 可选：<video>/<audio> 播放
xvfb-run -a --server-args="-screen 0 1280x720x24" \
  npx tsx src/cli/main.ts run examples/gui/hello.ts --ext gui
```

媒体 e2e 套件（`tests/e2e/gui-media.test.ts`）只有在 `gui.a`/`gui.lib` 与
`media.a`/`media.lib` 都存在时才会运行；`tests/e2e/gui-media-disabled.test.ts`
覆盖仅 GUI 归档的构建。

包清单集中在 `scripts/install-gui-deps.sh`（`apt-get install -y clang cmake
libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev
libxfixes-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev
libasound2-dev libpulse-dev libdbus-1-dev libudev-dev libdrm-dev libgbm-dev
libgl1-mesa-dev libegl1-mesa-dev libvulkan-dev mesa-vulkan-drivers xvfb`），
这样每个构建 `gui.a` 的 Linux job 都安装同一套依赖：默认使用 X11 后端，
Wayland 也已启用但尚未实际演练。任何构建该归档的地方都要先装它——没有这些
头文件，SDL3 找不到窗口后端。

