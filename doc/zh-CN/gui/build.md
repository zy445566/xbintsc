# 打包与构建

> 语言 / Language：[English](../../gui/build.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

- 源码位于 `runtime/ext_gui/`（C/C++），外加 `vendor/` 下的 vendored 库
  （被 gitignore；按需获取）。媒体解码位于 `runtime/ext_media/`，单独构建。
- `npm run gui`（`scripts/build-gui.ts`）获取固定版本的 SDL3（`SDL3_TAG`，
  默认 `release-3.2.10`），构建静态 SDL3，并获取、构建静态
  **glslang**（`GLSLANG_VERSION`，默认 `16.6.0`）、固定版本的 SPIR-V 头文件
  （`SPIRV_HEADERS_VERSION`）以及——**仅 macOS**——**SPIRV-Cross**
  （`SPIRV_CROSS_VERSION`，默认 `vulkan-sdk-1.4.313.0`）。glslang 是 WebGL
  背后的运行时 GLSL ES 编译器，SPIRV-Cross 把它的 SPIR-V 转成 Metal 后端使用的
  MSL（见 [WebGL](./webgl.md)），两者都与其他库一样合并进归档。Metal 是唯一消费
  MSL 的 SDL_GPU 后端，因此 Windows/Linux 上脚本既不获取也不合并 SPIRV-Cross：
  那里的 `webgl_msl.cpp` 编译为空，合并进来只会多出约 2.5 MiB 不可达代码。
  文本使用内置的 **stb_truetype**（`runtime/ext_gui/stb_truetype.h`），因此不再
  构建 FreeType/HarfBuzz。每个库（SDL3、glslang、SPIRV-Cross 与引擎）都用
  `-ffunction-sections -fdata-sections` 编译；SDL 关闭引擎从不调用的子系统
  （`SDL_JOYSTICK`、`SDL_HAPTIC`、`SDL_SENSOR`、`SDL_CAMERA`、`SDL_RENDER`、
  `SDL_OPENGL`/`SDL_OPENGLES`、`SDL_OFFSCREEN`、`SDL_HIDAPI`）。链接器随后丢弃
  不可达函数（`--gc-sections`、`-dead_strip` 或 link.exe 默认的 `/OPT:REF`）。
  随后编译引擎，并把
  所有内容合并到 `runtime/lib/<os>-<arch>/gui.a`（Windows 上 MSVC ABI 则为
  `gui.lib`），与 `core.a` 采用相同约定，静态打包这些库。合并使用 macOS 上的
  `libtool` 与其他平台上的 `ar -M`（GNU/LLVM）；CMake 归档会在构建根目录或
  `Release/` 下按任一生成器风格被发现。
- `npm run media`（`scripts/build-media.ts`）构建可选的 **FFmpeg** 后端。它获取
  FFmpeg（`FFMPEG_VERSION`，默认 `7.1`），配置一个只启用媒体元素所需
  demuxer/parser/decoder 的最小静态构建，用它编译
  `runtime/ext_media/media.cpp`，并把所有内容合并到
  `runtime/lib/<os>-<arch>/media.a`（Windows 上 MSVC ABI 则为 `media.lib`）。
  `gui.a` **不会**链接 FFmpeg：编译器为可能播放媒体的程序选中 `media` 链接特性后
  才链接 `media.a`（见[链接特性](#链接特性)）。
- `src/extensions/gui/index.ts` 通过 `nativeObjects()` 暴露引擎归档（经由
  `findRuntimeLibrary`，因此 `.a`/`.lib` 都可用），并通过 `linkerFlags()`
  暴露操作系统框架。它的 `linkFeatures()`（位于
  `src/extensions/gui/features.ts`）声明 `media`、`gpu`、`images` 与 `webgl`；driver 只在该特性
  启用时编译对应的 bridge。
- glslang（以及 macOS 上的 SPIRV-Cross）与 SDL3 一样合并进 `gui.a`，但引擎只
  通过 `gpu` 特性的分发表访问它；从不编译 GLSL 的程序不会取出这些归档成员。
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
  同一批目录还会打进可选的 `@xbintsc/<slug>` npm 包，因此发布覆盖的宿主平台从
  npm 安装后也能使用 `--ext gui`（`media`/`gpu`/`images`/`webgl` 自动选择）。归档在 Linux（Xvfb 下，使用
  lavapipe 软件 Vulkan 驱动）与 macOS 上构建并运行
  示例；Windows x64 也会构建两个归档并链接一个会触发 `media` 特性的 `gui` 程序
  （另有一条显式 `--feature media` 的构建），因此 MSVC
  构建在 CI 中得到验证，只是*运行*示例仍为试验性（托管 runner 没有 GPU/Vulkan
  驱动）。macOS 的运行同样是试验性的：Intel runner 的虚拟 GPU 缺少
  `MTLGPUFamilyMac2`，引擎会以 **77**（“此机器无法运行 GUI”）退出，任务记录为跳过，
  而不是去编译一套无法呈现的测试。Windows arm64 仍跳过两个归档（FFmpeg 的
  MSVC arm64 构建尚未验证）。
- Windows CI 通过 `.github/actions/vs-toolchain` →
  `scripts/setup-vs-toolchain.ps1` 使用与本地构建相同的 Visual Studio
  `vcvarsall` 环境和自带的 LLVM（不再单独安装 Chocolatey LLVM）。媒体构建时该
  action 还会通过 Chocolatey 安装 GNU make 并把 `xbintsc_MAKE` 指向它；FFmpeg
  源码与已安装的 `vendor/ffmpeg-build-msvc/dist` 前缀按脚本哈希缓存。
  configure/make 构建树本身不缓存（其中记录了另一台机器的绝对路径与编译选项）。
- 现有的缓存指纹已经会对 `nativeObjects()` 的内容与 `runtimeSources()` 的内容
  做哈希，因此重建任一归档都会自动使缓存二进制失效。
- CMake 构建树位于其源码**旁边**（`vendor/sdl-build-lean`、`vendor/glslang-build-lean`、
  `vendor/spirv-cross-build-lean`），绝不放在源码内部：`CMakeCache.txt` 记录的是配置它
  的那台机器，而 SDL 的视频后端是 `option()`，因此先运行的那次配置会一直生效。
  所以一个同时携带 `build/` 的 `vendor/SDL` 缓存树，会把某台机器的
  `SDL_X11`/`SDL_WAYLAND` 选择带进另一台机器的配置——即使头文件已安装，SDL 也会
  以 "SDL could not find X11 or Wayland development libraries" 中止。CI 缓存按
  OS/arch 区分键值，且不包含构建树；`npm run gui` 还会在 Debian/Ubuntu 上提前
  检查 X11/Wayland 头文件，并按名称报告缺失的开发包（当头文件位于该检查未知的
  前缀下时，可设置 `xbintsc_SKIP_HEADER_CHECK=1`）。

## 链接特性

用户只启用一个 `gui` 扩展，但其中的重型部分是可选的**链接特性**，按程序选择：

| 特性 | 链接什么 | 程序可能用到时 |
| --- | --- | --- |
| `media` | `media_bridge.c` + `media.a`（FFmpeg） | HTML 里有 `<video>`/`<audio>`、`createElement("video"/"audio")`、`new Audio()`、`decodeAudioData`，或运行时拼出的 HTML |
| `gpu` | `gpu_bridge.c`（实现本身已在 `gui.a`） | `getContext("webgl"/"webgl2"/"experimental-webgl")`（或运行时决定的 kind）、任意 `webgl` 字符串、`mix-blend-mode`/`mixBlendMode`，或运行时拼出的 HTML |
| `images` | `image_bridge.c`（实现本身已在 `gui.a`） | `<img>`、内联 `<svg>`、`background-image`/`url(...)`、`.svg` 路径、`drawImage`/`createImageBitmap`/`Image`，或运行时拼出的 HTML |
| `webgl` | `webgl_bridge.c`（实现本身已在 `gui.a`，依赖 `gpu`） | WebGL 的 `getContext(...)`、`webgl` 字符串，或运行时拼出的 HTML |

driver 会扫描打包后的 AST（`src/driver/usage.ts`）：所有字符串字面量（包括导入的
`.html` 与内联进来的 CSS）、标识符与属性名、每个 `getContext(...)` 的 kind，以及
HTML/CSS 是否可能来自编译器看不见的数据。简单的 `const` 别名会被解析；无法解析的
markup 会让程序按“动态 markup”保守处理，启用可选特性。

特性启用时，driver 只为本构建编译它的 C **bridge**，并链接在 `gui.a` 之前：

- `media` → `runtime/ext_media/media_bridge.c`；构造函数安装解码表并引用
  `xt_media_backend_*`，从而把 FFmpeg 成员从 `media.a` 里取出。
- `gpu` → `runtime/ext_gui/gpu_bridge.c`；构造函数调用 `gpu_feature.cpp` 里的
  `xt_gpu_feature_register`，安装 `XtGpuShaderApi` 表并引用 `compile_glsl` /
  `cross_compile_msl` / 反射函数，从而取出 glslang（macOS 上还有 SPIRV-Cross）。
- `images` → `runtime/ext_gui/image_bridge.c`；构造函数通过 `image_feature.cpp`
  安装 `XtImageApi`，其引用取出 stb_image 与 NanoSVG。layout、paint、canvas
  `drawImage` 与应用图标都走这张表，因此纯文本页面不链接这两个解码器。
- `webgl` → `runtime/ext_gui/webgl_bridge.c`；构造函数通过 `webgl_feature.cpp` 安装
  `XtWebglApi`，其引用取出 WebGL context 与 DOM 绑定。core 的 `getContext`、
  canvas 宽高 setter、`createImageBitmap`、host type 与 renderer 都走这张表，因此
  不用 WebGL 的页面不链接任何 `webgl_*`/`dom_api_webgl*`。

bridge 就是让链接器搜索归档的“强引用”；没有 bridge 时引擎的表保持 null，
实现成员永不被取出。因此大库可以留在现有归档里，不需要新的包布局。

覆盖方式：`--feature media,gpu,images,webgl`（或 `all`）强制开启；
`--no-feature media` 即使检测到也不链接。`webgl` 依赖 `gpu`，resolver 会一起
激活；配置应用图标会强制 `images`，因为图标走同一条解码管线。`--ext media` 是
`--ext gui --feature media` 的弃用别名。
自动检测到的特性归档缺失时构建打印 warning 并继续（元素/上下文按文档降级）；
显式强制的特性则报错。

> 检测是静态的。用 `fetch`、`readFileSync` 等运行时数据拼 HTML/CSS 的页面应传
> `--feature all`（或具体特性）。`--verbose` 会打印选中的集合，例如
> `xbintsc: link features: none`。

### 二进制体积

DOM-only 程序的小体积来自上述 lean 构建。Windows x64 上，一个 `hello` 规模的程序
从约 6.4 MiB 降到约 3.1 MiB：SDL 按上面的选项配置，function/data sections 让
`/OPT:REF` 丢弃页面触达不到的函数，WebGL/图片链接特性保持关闭，文本改用内置
stb_truetype 而不是 HarfBuzz + FreeType。剩余重量主要是静态 CRT、lean SDL
（约 1 MiB）、stb_truetype（约 0.1 MiB）以及 DOM/CSS/layout/paint 引擎本身
（约 1.2 MiB）。Linux 与 macOS 使用各自的链接器 section（`--gc-sections`/
`-dead_strip`），数字会有差异，但相对排序相同。

WebGL 与图片/SVG 是链接特性（约 0.4 MiB 的 WebGL context/DOM 代码与 0.23 MiB 的
stb_image/NanoSVG，DOM-only 页面不链接）。Canvas2D、Web Audio 与
`mix-blend-mode` 合成器尚未按需化；不使用它们的程序仍要付出其 API 面积（每项大约
0.1–0.2 MiB）。

文本替换是有意的取舍：stb_truetype 只接受 TrueType 轮廓，整形是逐码点映射加字体的
`kern` 表，因此 CFF/OTF 与可变字体、OpenType 连字以及复杂文种（阿拉伯/印度/泰文）
整形不可用；fallback 链仍会在主字体缺字时切换到其他字体。恢复 HarfBuzz/FreeType
会加回约 1.2 MiB；若必须恢复完整整形，平台文本栈（DirectWrite/CoreText）是另一条路。

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
`volume` 驱动播放。编译器从 `media.html` 里看到这些元素后会自动选中 `media`
特性；先用 `npm run media` 构建一次 `media.a`。
媒体 `src` 相对于进程工作目录解析，因此从仓库根目录运行：
`xbintsc run examples/gui/media.ts --ext gui`。几何报告见该目录的 README。

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

