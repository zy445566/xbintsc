# 扩展：Node 模块、GUI、链接特性、C++/Rust 库

编译器核心与平台无关。任何与平台相关的东西——Node 的 `fs`、一个 HTML/CSS 渲染器、
你自己的 C++ 库——都以**扩展**的形式接入：它既贡献模块绑定，也贡献要一起编译/链接的
C/C++ 源码或目标文件。扩展里的重型可选部分（FFmpeg、glslang）做成**链接特性**，
由程序用法选择，而不是单独的扩展开关。

## 最容易踩的规则

**只要扩展没开启，import 它提供的模块就会失败。** xbintsc 知道该模块存在，并明确
告诉你要传什么：

```bash
$ xbintsc run app.ts            # app.ts: import { readFileSync } from "fs";
error TS6001: module 'fs' is provided by the 'node' extension; pass --ext node
```

```bash
xbintsc run app.ts --ext node
```

多个扩展用逗号分隔一次开启：`--ext node,gui`。在项目里写进
`xbintsc.config.json` 就不需要每次带参数：

```json
{ "entry": "src/app.ts", "outDir": "build", "extensions": ["node"] }
```

## 内置扩展

### `node` —— Node 内置模块

用 `--ext node` 开启。可以用裸名或 `node:` 前缀导入（`import { readFileSync } from
"node:fs"`），两种写法解析到同一实现。

| 模块 | 说明 |
| --- | --- |
| `fs` | **仅同步 API**（`readFileSync`、`writeFileSync` 等）；`fs/promises` 是独立模块 |
| `fs/promises` | 基于 Promise 的文件 API；阻塞 I/O 跑在异步 I/O 线程池上并在 reactor 上 settle |
| `path` | 同时接入命名空间分发，`path.join(...)` 可用 |
| `os` | |
| `process` | `process.cwd()`、`argv`、`env` 等；也接入命名空间分发 |
| `buffer` | |
| `crypto` | |
| `stream`、`stream/promises` | |
| `events` | |
| `net`、`dgram`、`http` | 跑在 xbintsc 事件循环上的套接字与服务端（注意下面的异步限制） |
| `child_process` | |
| `worker_threads` | |
| `util`、`querystring`、`url`、`assert`、`test`、`zlib` | |

各模块的具体覆盖（确切函数与选项）见
[../../zh-CN/implemented/node.md](../../zh-CN/implemented/node.md)，缺失项见
[../../zh-CN/unimplemented/node.md](../../zh-CN/unimplemented/node.md)。

在 `net`/`http` 上搭东西之前，先记住运行时模型：事件循环在**程序主体之后**才运行，
但 `async`/`await` 会在 pending Promise 上挂起，因此由套接字回调、定时器或 offload 的
文件/HTTP job settle 的 Promise 都可以 await。`net.connect` 本身仍是阻塞的。见
[language-support.md](./language-support.md)。

### `gui` —— HTML/CSS 窗口

用 `--ext gui` 开启并导入 `gui` 模块：

```ts
import { createWindow, run } from "gui";
```

它是自带渲染引擎的 GPU 加速实现（自己的 HTML 解析器、CSS 级联、布局与合成器），不是
系统 WebView。编译器一侧还会把本地 `<link rel="stylesheet">` 及其 `@import` 内联为
`<style>` 块（运行时解析器只读取内联 `<style>`）；并让随包引入的 Three.js 的
`WebGLRenderer` 先探测 WebGL 上下文：引擎有上下文时使用 Three.js 自己的渲染器，
直接使用真实的 WebGL 2 后端（只修补其着色器循环展开，不再打包 Canvas2D 回退）。
`<img>` 由引擎自身解码；`<video>`/`<audio>` 解码（FFmpeg）与 GLSL 翻译（glslang）
是**可选的链接特性**，由程序自动选择——见
[可选链接特性](#可选链接特性media-gpu-images-webgl)。也支持 `<canvas>`：
`getContext("2d")` 返回的上下文，其填充、路径、
渐变、文本与 `drawImage` 由自研 CPU 光栅器处理（另有线段端点/连接、虚线描边、
`ellipse` 与 `roundRect`），元素句柄还暴露 `width`/`height`。CSS
`transform`/`transform-origin` 作为绘制期效果可用，因此一个盒子（以及其中的文本、
图像与子元素）可以被平移、缩放与旋转而不触发任何重排——可以用
`requestAnimationFrame` 逐帧驱动，或写成 `transition: transform`（见
`examples/gui/pelican-svg`）。CSS 子集还覆盖 `@media`、`::before`/`::after`
生成内容、线性/径向多层背景、`text-shadow`、`clip-path` 与 `filter`
颜色/阴影效果；`backdrop-filter` 与真正的组模糊是有文档说明的近似或未支持。
因为主体是 C++，它以各平台预编译的 `gui.a`/`gui.lib` 形式分发。如果该
平台的归档不存在，构建会以明确的提示失败；设计与路线图见 [../../zh-CN/gui.md](../../zh-CN/gui.md)（总览；引擎按功能拆分在 [`doc/zh-CN/gui/`](../../zh-CN/gui/) 下），
脚本接口见 [../../zh-CN/gui-scripts.md](../../zh-CN/gui-scripts.md)。Canvas API
及其限制见 [../../zh-CN/gui/canvas.md](../../zh-CN/gui/canvas.md)。

为浏览器编写的页面还能直接使用它们依赖的浏览器全局 —— `window`/`self`、`document`、
`navigator`、`location`、`localStorage`、`requestAnimationFrame`、`matchMedia`、
`getComputedStyle`、`alert`/`prompt`、`escape`、`Event` 家族，以及可用的 Web Audio
实现（`AudioContext` 提供 gain、oscillator、buffer-source、biquad、compressor、
stereo-panner、constant-source 与 delay 节点、真实的 48 kHz 混音器、逐采样的
AudioParam 自动化，`decodeAudioData` 引擎内解码 WAV，压缩格式则通过 `media` 特性）—— 另外还有帧泵：
它在 `loadHTML` 时启动，仅在仍有动画帧回调时重新排程，因此从不调用 `run()` 的页面
也能绘制并最终退出。**WebGL 1.0 与 2.0 已实现**：`canvas.getContext("webgl")` /
`"webgl2"` 返回 GPU 上下文，其 GLSL ES 着色器由内置 glslang 编译为 SPIR-V
（Vulkan 直接消费，Metal 上由内置 SPIRV-Cross 转成 MSL），
并通过 SDL_GPU 绘制；页面直接合成颜色纹理而无须 CPU 读回，WebGL 2 还增加
VAO、实例化、整数属性、采样器对象、`texStorage2D` 与缓冲拷贝（见
[../../zh-CN/gui/webgl.md](../../zh-CN/gui/webgl.md)）。WebXR 与流式接口**尚未实现**：
`typeof` 会回答
`"undefined"`，让页面走自己的降级分支。`fetch` 不是 GUI 扩展的功能——它由核心
运行时提供（`http://`，以及本地 `file://` 与相对路径；不支持 HTTPS 与流式），
因此 GUI 程序同样可用。见 [../../implemented/fetch.md](../../implemented/fetch.md)。

### 可选链接特性（media、gpu、images、webgl）

`gui` 仍然只有一个扩展，但其中的重型部分是可选的**链接特性**。编译器扫描打包后的
HTML/CSS/TS（导入的 `.html` 与内联 CSS 都属于 bundle），按程序自动选择；不需要额外
扩展参数：

- **`media`** —— `<video>`/`<audio>` 与压缩格式 `decodeAudioData` 的 FFmpeg 解码。
  由 `<video>`/`<audio>` 元素、`createElement("video"/"audio")`、`new Audio()`、
  `decodeAudioData` 或运行时拼出的 markup 触发，链接 FFmpeg 后端
  `media.a`/`media.lib`。
- **`gpu`** —— WebGL 与 `mix-blend-mode` 共用的 glslang/SPIRV-Cross GLSL 翻译器。
  由 `getContext("webgl"/"webgl2"/…)`（或运行时决定的 kind）、任意 `webgl` 字符串、
  `mix-blend-mode` 触发。
- **`images`** —— `<img>`、内联 `<svg>`、CSS `background-image`/`url(...)`、
  `drawImage`/`createImageBitmap`/`Image` 与应用图标所需的 stb_image/NanoSVG 解码。
  纯文本页面两个解码器都不链接。
- **`webgl`** —— WebGL context 与 DOM 绑定。由 WebGL 的 `getContext(...)` 或
  `webgl` 字符串触发；它依赖 `gpu`，两者一起激活。不用 WebGL 的页面两者都不链接。

用 `--feature media,gpu,images,webgl`（或 `all`）覆盖扫描，用 `--no-feature media`
强制不链接。`--verbose` 会打印选中的集合。只渲染 HTML/CSS/2D canvas 的程序既不链接
FFmpeg、glslang，也不链接图片解码器或 WebGL context；用运行时数据拼 HTML/CSS 的
页面应传 `--feature all`，因为扫描看不到它。
自动检测到的特性归档缺失时构建打印 warning，能力在运行时降级（媒体元素触发
`error`）；显式强制的特性则报错。

DOM API 与普通 HTML 媒体元素一致：`play()` / `pause()` / `load()`，以及
`paused` / `ended` / `duration` / `currentTime` / `volume` / `muted` / `loop` /
`autoplay` / `videoWidth` / `videoHeight` / `readyState` / `src` 属性，还有
`loadedmetadata`/`play`/`pause`/`ended`/`timeupdate`/`error` 事件。没有内置
控件——用元素和 CSS 自绘控件（见 `examples/gui/media.ts`）。视频帧与音频 PCM
都在主线程解码；视频走图像纹理路径。Windows 上构建归档需要 POSIX shell（Git for
Windows 自带的 bash）与原生 GNU make（Chocolatey `make`，或 `xbintsc_MAKE`）。见
[../../zh-CN/gui/build.md](../../zh-CN/gui/build.md) 与
[../../zh-CN/gui/media.md](../../zh-CN/gui/media.md)。

## 第三方 npm 包

没有被已启用扩展认领的裸说明符，会在 `node_modules` 里查找并**作为源码**打包，
CommonJS 包也在支持范围内（打包器会给它合成 `module`/`exports`，并降级其中的
`require` 调用）。不支持的场景：动态 `require(<表达式>)`、依赖 `__dirname` 等 Node
全局变量的包，以及循环依赖。自己源码里的 `require()` 会被拒绝并提示改用 `import`。

实践结论：写 Node 程序时，正确做法通常就是开 `--ext node`；只有确实必要时才去依赖
npm 包。

## 原生扩展（C++ / Rust，无需改编译器）

任何暴露了运行时 ABI 的 `extern "C"` 入口的代码都能被链接进来：

```c
xt_value my_fn(int32_t argc, xt_value *argv);
```

用**外部**工具链（clang++ 或 cargo）构建它，把产物写进一个 JSON 清单，然后传清单：

```bash
xbintsc build demo.ts --ext-native ./xbintsc.manifest.json
xbintsc build demo.ts --ext-native a.json,b.json      # 多个
```

```jsonc
{
  "name": "mathx-cpp",                       // 必填，唯一
  "objects": ["build/mathx.o"],              // .o / .a / .lib，相对本文件
  "linkerFlagsByPlatform": {                 // C++/Rust 运行时
    "linux":  ["-lstdc++", "-lm"],
    "darwin": ["-lc++"],
    "win32":  ["-lmsvcprt"]
  },
  "builtins": { "cppClamp": { "symbol": "mathx_clamp" } },   // 无需 import
  "modules": {
    "mathx": { "exports": { "add": { "symbol": "mathx_add" } } }
  }
}
```

之后 `import { add } from "mathx"` 会像 C 运行时绑定一样降级到原生符号；`builtins`
无需导入即可全局调用。

编写辅助头：`runtime/xt_ext.h`（C/C++）与 `runtime/xt_ext.rs`（Rust），它们封装了参数
与值的辅助函数。关键 ABI 事实：值是 64 位 NaN-boxed 字；字符串**不**以 NUL 结尾，
所以要把 `xt_string_data` 与 `xt_string_length_value` 配对使用；运行时交给你的内存
不会被移动或释放。请使用与 xbintsc 解析到的同一个 clang/ABI 构建（`xbintsc doctor`
会打印它）。Windows 上必须是面向 MSVC 的 clang，通常就是 Visual Studio 自带的
LLVM；不需要开发者命令提示符。

可直接照抄的工程：[`examples/extensions/cpp`](../../../examples/extensions/cpp) 与
[`examples/extensions/rust`](../../../examples/extensions/rust)；完整指南见
[../../../examples/extensions/README.md](../../../examples/extensions/README.md)。

## 编程式注册

```ts
import { build, createDefaultRegistry, nativeExtensionFromManifest, nodeExtension } from "xbintsc";

const extensions = createDefaultRegistry()
  .register(nodeExtension)
  .register(nativeExtensionFromManifest("./xbintsc.manifest.json"));

build("demo.ts", { extensions });
```

注册表也是宿主工具提示"某扩展未开启"的机制，正是它把错误变成了可操作的
"pass `--ext node`"，而不是下游令人困惑的报错
（[../../../src/extensions/catalog.ts](../../../src/extensions/catalog.ts)）。内置扩展
列表由 `bundledExtensions()` 提供。
