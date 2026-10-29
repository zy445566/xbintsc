# GUI API

> 语言 / Language：[English](../../gui/api.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

## 面向 TS 的 API

```ts
import { createWindow, run, quit } from "gui";

const win = createWindow({ title: "Demo", width: 900, height: 600 });
win.setBackground("#14161c");
win.loadHTML(INDEX_HTML);              // 解析 HTML/CSS 并计算样式
win.on("ready", () => console.log("first frame presented"));
win.on("close", () => console.log("window closed"));

run();                                 // 驱动主循环，直到所有窗口关闭
```

窗口句柄上已实现的方法：`setTitle` / `setSize` / `loadHTML` /
`getHTML` / `setBackground` / `close` / `isOpen` / `on` / `off`，以及
`driver()`——它报告当前使用的 SDL_GPU 后端（`"vulkan"`、`"direct3d12"`、
`"metal"` 等，由 SDL 挑选设备支持的最佳后端）。同时也提供 Web 写法：
`addEventListener` / `removeEventListener` 接受标准的选项对象（`capture`、
`once`、`passive`、`signal`），`closed` getter 对应 `isOpen()`，因此按浏览器
API 编写的文档可以原样运行。

元素监听器支持同一组选项：`{ once }` 在首次调用后移除监听器，`{ passive: true }`
使 `preventDefault()` 变成无操作，`{ signal }`（来自 `new AbortController()` 的
`AbortSignal`）在 abort 时移除监听器，包括添加时就已经 abort 的信号。
`element.dispatchEvent(event)` 派发的就是**同一个**事件对象（身份可观察），并填充
`target` / `currentTarget` / `eventPhase`、记录路径供 `event.composedPath()` 使用。
`new Event(...)`、`new CustomEvent(...)`、`new MouseEvent(...)`、
`new KeyboardEvent(...)`、`new PointerEvent(...)`、`new WheelEvent(...)`、
`new FocusEvent(...)`、`new InputEvent(...)` 与 `new SubmitEvent(...)` 会保留 `init`
对象的全部字段（`detail`、`composed`、鼠标/键盘成员等）。窗口监听器接受同一组
选项（`capture` 参与监听器身份匹配；窗口投递没有捕获阶段）。

窗口导航是真实实现的：`scrollTo(x, y)` / `scrollTo({ left, top })` /
`scrollBy(...)` 移动页面视口（会按内容边界夹取，绘制与命中测试随之偏移），
`scrollX` / `scrollY` / `pageXOffset` / `pageYOffset` 报告偏移量。`focus()`
提升操作系统窗口并置位 `document.hasFocus()`，`blur()` 清除它。事件：
`ready`（首帧呈现后）、`load`、`close`、`resize`、`scroll`、`focus`、`blur`，
以及输入事件 `mousemove`、`mousedown`、`mouseup`、`click`、`wheel`、
`keydown`、`keyup`。

`createWindow(options)` 除标题与尺寸外还支持：`{ title, width, height,
highDensity, resizable, borderless, alwaysOnTop, hidden, fullscreen, x, y,
opacity, maximized, minimized }`，它们会映射到对应的 SDL 窗口标志/调用，而不是
被忽略。

`window.confirm(message)` / `window.prompt(message, defaultValue)` 不会再丢弃
参数：设置 `XT_GUI_DIALOGS=1` 且存在窗口时显示原生消息框；否则把消息写到 stderr
并返回文档化的默认值（`confirm` → `false`，`prompt` → `defaultValue` 或 `""`）。

在 Windows 上，GUI 程序会被链接为 **Windows 子系统**可执行文件，因此启动时不会闪出控制台
窗口；这也意味着从资源管理器启动时 stdout/stderr 无处可去，调试时可在
`xbintsc.config.json` 里设置 `app.console: true` 保留控制台子系统。

没有可用 GPU 的宿主无法运行 GUI。当 SDL 无法打开视频驱动，或无法创建
Metal/Vulkan/D3D12 设备时，引擎会打印原因并以状态码 **77** 退出——这是 autotools
的“跳过”约定，CI 与脚本因此可以区分“这台机器没有 GUI”和“程序失败”。此后若窗口仍
无法打开（SDL 窗口或 GPU claim 错误），`createWindow` 会抛出 JavaScript `Error`，
而不再返回 `undefined` 句柄、让下一行以 `Cannot read properties of undefined` 失败。

触发的事件：
`ready`（首个呈现帧之后）、`load`、`close`、`resize`，以及输入事件
`mousemove`、`mousedown`、`mouseup`、`click`、`wheel`、`keydown`、`keyup`。

## 浏览器全局

从 Web 迁移过来的页面很少只用到 `createWindow`。启用 `--ext gui` 后，运行时同样提供
这类移植需要的那一层浏览器接口，程序不必自己声明：

```ts
const ctx = new AudioContext();            // 也可 window.AudioContext
ctx.createGain().connect(ctx.destination);
document.querySelector("#start").addEventListener("click", () => {});
console.log(navigator.userAgent, window.innerWidth, location.href);
requestAnimationFrame(frame);
```

| 全局 | 取值 |
| --- | --- |
| `window`、`self` | 最近打开的窗口句柄（若程序尚未创建窗口，会隐式打开一个），因此 `window.innerWidth`、`window.addEventListener`、`window.setTimeout`、`window.localStorage`、`window.location`、`window.devicePixelRatio`、`window.matchMedia` 都可用 |
| `document` | 该窗口的 document 句柄 |
| `navigator` | `userAgent`（以 `xbintsc` 结尾的桌面 Chrome 字符串）、`platform`、`vendor`、`language(s)`、`hardwareConcurrency`、`maxTouchPoints`、`onLine`、`vibrate()` |
| `localStorage` | 内存中的 `getItem` / `setItem` / `removeItem` / `clear` / `key` / `length` |
| `AudioContext`、`webkitAudioContext` | 带真实混音器与解码器的 Web Audio 图（见下） |
| `Event`、`CustomEvent`、`ProgressEvent`、`MouseEvent`、`KeyboardEvent`、`PointerEvent`、`WheelEvent`、`FocusEvent`、`InputEvent`、`SubmitEvent` | 事件构造器；`init` 参数按原样保留（`detail`、`composed`、鼠标/键盘字段等） |
| `AbortController`、`AbortSignal` | 真实的 controller/signal 对，用于 `{ signal }` 移除监听器，支持 `abort()`、`aborted`、`reason`、`onabort` 与 `throwIfAborted()` |
| `HTMLCanvasElement`、`HTMLImageElement`、`WebGLRenderingContext`、`WebGL2RenderingContext`、`WebGLBuffer`/`WebGLTexture`/`WebGLShader`/`WebGLProgram`/`WebGLFramebuffer`/`WebGLRenderbuffer`/`WebGLUniformLocation`/`WebGLActiveInfo`/`WebGLVertexArrayObject`/`WebGLSampler`/`WebGLQuery`/`WebGLSync`/`WebGLTransformFeedback` | 真实的构造函数值。`canvas.getContext("webgl")` 与 `getContext("webgl2")` 返回 GPU 上下文（见 [WebGL](./webgl.md)）。 |
| `ImageBitmap`、`createImageBitmap(source)` | 由已解码像素支撑的不可变位图（`<img>`、`<canvas>`、`<video>` 帧、`OffscreenCanvas` 与 `ImageData` 形状）；promise 立即 settle，`texImage2D` 可直接接受该对象 |
| `OffscreenCanvas` | 真正的 2D 离屏位图，带 `getContext("2d")`、`width`/`height`、`transferToImageBitmap()` 与 `convertToBlob()`；`canvas.transferControlToOffscreen()` 会把元素位图交给它（见 [Canvas](./canvas.md)）。离屏 WebGL 上下文不提供。 |
| `ImageData`、`XRWebGL*`、`Request`、`Headers`、`Response`、`ReadableStream`、`DOMParser` | `typeof` 探测返回 `"undefined"`（没有 XR/fetch/流式后端），让页面走自己的“不支持”分支，而不是调用空壳。Three.js 页面在本后端上使用真实渲染器（见[已知问题](./known-issues.md)）。 |
| `requestAnimationFrame` / `cancelAnimationFrame` | 排入窗口的动画帧列表，见[运行时集成](./runtime.md) |
| `alert` / `confirm` / `prompt`、`matchMedia`、`getComputedStyle`、`escape` | 宿主实现。`matchMedia` 返回**实时**列表（按查询缓存）：`matches` 在窗口尺寸变化时重新求值，翻转时触发 `change` 监听器。`confirm`/`prompt` 仅在 `XT_GUI_DIALOGS=1` 时使用原生对话框，否则把消息写到 stderr 并返回文档化的默认值（Annex B `escape` 按 UTF-16 码元编码，与 `decodeURIComponent(escape(bytes))` 一致） |

`getElementById` / `querySelector` 在 `loadHTML` 之前也能回答：此时得到的是**挂起**句柄，
首次使用时才绑定到真实节点，因此在模块初始化阶段缓存 `document.querySelector("#id")`
的页面依然可用。页面解析完成后未命中则回答 `undefined`，与浏览器一致。

### Web Audio

`new AudioContext()` 会构建**真正参与混音的音频图**：`runtime/ext_gui/audio_mixer.cpp`
以 48 kHz 立体声 float、128 帧为一个渲染量子进行混音，每个节点对多路输入求和；
首次 `start()`/`resume()` 会尽力打开默认 SDL 播放设备（无头环境下保持静音但仍在渲染）。
音频在 SDL 音频线程上基于原生状态渲染，页面无须自行驱动，音频线程也不会触碰任何运行时值。

已实现的节点：`createGain`、`createOscillator`（sine、square、sawtooth、triangle）、
`createBufferSource`（buffer、loop、loopStart/loopEnd、playbackRate/detune）、
`createBiquadFilter`（lowpass、highpass、bandpass、lowshelf、highshelf、peaking、notch、allpass）、
`createDynamicsCompressor`、`createStereoPanner`、`createConstantSource` 与
`createDelay(maxDelayTime)`。`connect` 返回目标节点，因此可链式连接
（`src.connect(gain).connect(ctx.destination)`）；`disconnect()` / `disconnect(target)`
会更新原生图；`start(when, offset, duration)` / `stop(when)` 按图的时钟生效。
列表之外的节点（`createAnalyser`、`createConvolver`、`createWaveShaper`、
`createChannelSplitter`、`createPeriodicWave` 等）不提供，其方法就是缺失的。

每个 `AudioParam` 都有实时 `value`，以及 `setValueAtTime` / `linearRampToValueAtTime` /
`exponentialRampToValueAtTime` / `setTargetAtTime` / `cancelScheduledValues` /
`cancelAndHoldAtTime`。自动化时间线**逐采样**求值，因此页面空闲时斜坡仍会继续推进；
`currentTime` 随渲染量子前进。

`createBuffer(channels, length, sampleRate)` 返回的 AudioBuffer 的
`getChannelData(i)` 是真正的 `Float32Array`，可以用代码逐样本填充。
`decodeAudioData(bytes)` 返回解码后的 AudioBuffer Promise：WAV（PCM 8/16/24/32 位、
IEEE float 32/64、A-law 与 µ-law）由引擎自身解码；MP3、AAC/M4A、Vorbis、Opus 与 FLAC
需要可选的 `media` 扩展（`--ext gui,media`），以其中的 FFmpeg 解码器作为回退。
`bytes` 可以是 ArrayBuffer、类型化数组、Node `Buffer`，或 `fs.readFileSync`
返回的原始字节字符串（本运行时没有专门的二进制类型，而该字符串的存储正是文件字节）。
解码结果会重采样到上下文的 48 kHz，因此 `buffer.sampleRate` 与
`context.sampleRate` 一致；若没有可用的解码器，Promise 会以 `EncodingError` 拒绝。

`resume()` / `suspend()` / `close()` 控制设备与 `state` 属性；`sampleRate` 为 48000，
与媒体解码器一致。

测试与诊断可以创建**离线**上下文同步渲染：

```ts
const ctx = new AudioContext({ __offline: true });
// … 构建音频图并启动音源 …
const samples = ctx.__render(480);   // 交错立体声 Float32Array
```

这个私有钩子不会打开设备，也不属于 Web Audio API；`tests/e2e/gui-audio.test.ts`
用它来固定混音数学。

输入处理器接收单个负载对象（生命周期处理器不接收任何参数）：

```ts
win.on("click", (e) => console.log(e.x, e.y, e.button, e.target));
win.on("wheel", (e) => console.log(e.deltaX, e.deltaY));
win.on("keydown", (e) => console.log(e.key, e.code, e.ctrlKey, e.shiftKey));
```

`document.addEventListener("keydown", handler)` 会在事件从聚焦元素或 `<body>`
冒泡后收到同一个事件，早于 window 监听器。`code` 是物理键并采用浏览器写法
（`"KeyW"`、`"ArrowUp"`、`"Space"`）；`key` 是产生的值（`"w"`/`"W"`、
`"ArrowUp"`、`" "`）。见 [input.md](./input.md)。

| 字段 | 事件 | 含义 |
| --- | --- | --- |
| `x`、`y` / `clientX`、`clientY` | pointer、wheel | 相对于视口的逻辑像素 |
| `button` | pointer | `0` 左键、`1` 中键、`2` 右键（移动时为 `-1`） |
| `clicks` | pointer | 操作系统报告的点击次数 |
| `deltaX`、`deltaY` | wheel | 滚动量（`deltaY` 为正表示向下） |
| `key`、`code` | keyboard | 浏览器 `KeyboardEvent` 写法：产生的值与物理键 |
| `repeat`、`ctrl`、`shift`、`alt`、`meta` | keyboard | 修饰键（旧版 DOM 写法） |
| `ctrlKey`、`shiftKey`、`altKey`、`metaKey` | keyboard | 修饰键（现行 DOM 写法） |
| `target` | pointer、wheel | 该点下最深的元素，表示为类 CSS 描述符（`div#main.card`），可能缺省 |

## 诊断

为了让 HTML/CSS/绘制流水线在没有 GPU 的情况下也可测试，窗口句柄暴露只读钩子：

```ts
win.computedStyle(selector, property)  // e.g. ("#main", "width") -> "60%"
win.queryCount(selector)               // 匹配元素的数量
win.getBoundingClientRect(selector)    // { x, y, width, height }（边框盒，含变换）
win.documentTree()                     // 序列化的 DOM（调试用）
win.layoutTree()                       // 序列化的布局盒（调试用）
win.paintCount()                       // 显示列表中填充矩形的数量
win.paintList()                        // 序列化的显示列表（调试用）
win.inlineSvg(selector)                // 序列化的内联 <svg>：{ markup, width, height, shapes }
win.measureText(text, fontSize?, family?)  // 整形后的推进宽度（像素）
win.fontMetrics(fontSize?, family?)    // { ascent, descent, lineHeight, ready }
win.hitTest(x, y)                      // 最深的元素描述符，或 ""
win.sendEvent(type, options?)          // 合成输入（测试用）
win.advance(ms)                        // 推进 CSS transition 时钟（测试用）
win.stats()                            // 引擎/运行时计数器（内存排查用）
```

`win.inlineSvg(selector)` 返回引擎为第一个匹配 `selector` 的 `<svg>` 元素构建的
独立 SVG 文档（`markup`），以及 NanoSVG 从它解析出的固有尺寸与图形数量；没有
匹配或元素不是 `<svg>` 时返回 `undefined`。它让序列化器的规范大小写
（`viewBox`、`linearGradient`、`gradientUnits` 等）无需 GPU 即可测试——HTML
解析器会把它们全部小写化。

`win.stats()` 返回 `{ heapBytes, heapAllocations, treeNodes, detachedRoots,`
`detachedNodes, handles, handleSlots, listeners }`：运行时存活堆、DOM 树规模、
分离节点池（detached pool）以及元素句柄表。稳定状态下它们都应保持常量——若
`detachedNodes` 或 `handles` 持续上升，说明程序移除节点的速度快于释放速度
（见[节点生命周期](./dom.md)）。

同一个对象还报告上一帧做了什么以及程序累计做了多少工作，渲染开销就是靠它度量
（见[性能](./rendering.md#性能m20)）：

| 字段 | 含义 |
| --- | --- |
| `frameMs`、`restyleMs`、`layoutMs`、`paintMs`、`uploadMs`、`drawMs` | 上一帧各阶段耗时（毫秒） |
| `frames`、`presents`、`exposes`、`deferred`、`skippedFrames`、`starvedFrames`、`transitionFrames` | 帧循环活动：`presents` 是提交呈现的帧数，`exposes` 是窗口系统用 expose/shown/restore 事件要求的呈现帧数，`deferred` 是取不到可绘制目标而被推迟的呈现帧数，`skippedFrames` 是无需提交的帧数，`starvedFrames` 是要求呈现却拿不到可绘制目标的帧数 |
| `restyles`、`restyleNodes`、`rulesEvaluated`、`mediaEvaluated`、`layouts`、`layoutBoxes` | 层叠与布局的工作量 |
| `textShapeCalls`、`textShapeHits`、`textCacheBytes`、`textCacheEntries` | 整形缓存的未命中/命中与占用 |
| `paints`、`paintOps`、`drawRuns`、`drawCalls`、`uploadSubmits`、`vertices`、`syncNodes` | 显示列表、绘制调用与上传的工作量 |
| `canvasDraws`、`canvasUploads`、`canvasUploadBytes` | Canvas 2D 绘制次数、纹理上传次数与拷贝字节数（局部重绘只上传脏矩形） |
| `webglDraws`、`webglPasses`、`webglPipelineMisses`、`webglBufferUploadBytes` | WebGL 绘制次数、打开的渲染 pass 数、新建管线数与缓冲拷贝字节数 |

除 `*Ms` 时间外，所有字段都是累计值，因此两次快照相减即得到一帧的工作量。
`restyleMs` 覆盖层叠**以及**它触发的布局过程（布局过程本身也单独报告为
`layoutMs`）。`presents - exposes - deferred` 才是仅由页面要求的呈现帧数：
expose/shown/restore 事件表示窗口内容已丢失、必须重绘，而具体时机由窗口服务器
决定；deferred 呈现则是重放某一帧在取不到交换链图像时已经构建好的内容，
`starvedFrames` 统计这些帧（该值持续增长说明窗口完全没有被更新）。
`XT_GUI_PERF=1` 会把同样的数字打印到 stderr（逐帧一行；`XT_GUI_PERF=<N>` 则每
N 帧取平均）。

它们被 `tests/e2e/gui-*.test.ts` 用来断言解析、选择器匹配、特异性、继承、
`!important` 与布局几何。它们在之后仍可用于调试。

## 多窗口

多窗口由对象模型自然得出：`createWindow` 返回一个原生对象句柄；每个句柄
拥有自己的 `SDL_Window`/GPU 表面与自己的 DOM 树。`run()` 驱动一个共享主循环，
它每帧 tick 所有窗口，并在最后一个窗口关闭时退出。