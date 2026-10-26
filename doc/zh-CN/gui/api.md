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
`addEventListener` / `removeEventListener` 是 `on` / `off` 的别名（其
`options` 参数会被接受并忽略），`closed` getter 对应 `isOpen()`，因此按浏览器
API 编写的文档可以原样运行。

在 Windows 上，GUI 程序会被链接为 **Windows 子系统**可执行文件，因此启动时不会闪出控制台
窗口；这也意味着从资源管理器启动时 stdout/stderr 无处可去，调试时可在
`xbintsc.config.json` 里设置 `app.console: true` 保留控制台子系统。

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
| `AudioContext`、`webkitAudioContext` | Web Audio 图构建器（见下） |
| `Event`、`CustomEvent`、`ProgressEvent` 等 | 事件构造器 |
| `HTMLCanvasElement`、`HTMLImageElement`、`WebGLRenderingContext`、`WebGL2RenderingContext`、`WebGLBuffer`/`WebGLTexture`/`WebGLShader`/`WebGLProgram`/`WebGLFramebuffer`/`WebGLRenderbuffer`/`WebGLUniformLocation`/`WebGLActiveInfo`/`WebGLVertexArrayObject`/`WebGLSampler`/`WebGLQuery`/`WebGLSync`/`WebGLTransformFeedback` | 真实的构造函数值。`canvas.getContext("webgl")` 与 `getContext("webgl2")` 返回 GPU 上下文（见 [WebGL](./webgl.md)）。 |
| `ImageBitmap`、`createImageBitmap(source)` | 由已解码像素支撑的不可变位图（`<img>`、`<canvas>`、`<video>` 帧与 `ImageData` 形状）；promise 立即 settle，`texImage2D` 可直接接受该对象 |
| `ImageData`、`OffscreenCanvas`、`XRWebGL*`、`Request`、`Headers`、`Response`、`ReadableStream`、`DOMParser` | `typeof` 探测返回 `"undefined"`（没有 XR/fetch/流式后端），让页面走自己的“不支持”分支，而不是调用空壳。Three.js 页面在本后端上使用真实渲染器（见[已知问题](./known-issues.md)）。 |
| `requestAnimationFrame` / `cancelAnimationFrame` | 排入窗口的动画帧列表，见[运行时集成](./runtime.md) |
| `alert` / `confirm` / `prompt`、`matchMedia`、`getComputedStyle`、`escape` | 宿主实现（Annex B `escape` 按 UTF-16 码元编码，与 `decodeURIComponent(escape(bytes))` 一致） |

`getElementById` / `querySelector` 在 `loadHTML` 之前也能回答：此时得到的是**挂起**句柄，
首次使用时才绑定到真实节点，因此在模块初始化阶段缓存 `document.querySelector("#id")`
的页面依然可用。页面解析完成后未命中则回答 `undefined`，与浏览器一致。

### Web Audio

`new AudioContext()` 会构建音频图，但**不做混音也不渲染音频**：背后没有后端。已实现的是
页面运行与自检所需的接口 —— `createGain` / `createOscillator` / `createBufferSource` /
`createBiquadFilter` / `createDynamicsCompressor` / `createBuffer`（真实的
`Float32Array` 声道）/ `decodeAudioData`（以空缓冲区兑现）/ `resume` / `suspend` /
`close`，`AudioParam` 的 `value` 与 `setValueAtTime` / `linearRampToValueAtTime` /
`exponentialRampToValueAtTime` / `setTargetAtTime` / `cancelScheduledValues` /
`cancelAndHoldAtTime`，节点 `connect`（返回目标节点，因此可链式连接）/ `disconnect` /
`start` / `stop`。`sampleRate` 为 48000、`state` 为 `"running"`，`currentTime` 仅随
事件循环推进。

输入处理器接收单个负载对象（生命周期处理器不接收任何参数）：

```ts
win.on("click", (e) => console.log(e.x, e.y, e.button, e.target));
win.on("wheel", (e) => console.log(e.deltaX, e.deltaY));
win.on("keydown", (e) => console.log(e.key, e.code, e.ctrl, e.shift, e.alt, e.meta));
```

| 字段 | 事件 | 含义 |
| --- | --- | --- |
| `x`、`y` / `clientX`、`clientY` | pointer、wheel | 相对于视口的逻辑像素 |
| `button` | pointer | `0` 左键、`1` 中键、`2` 右键（移动时为 `-1`） |
| `clicks` | pointer | 操作系统报告的点击次数 |
| `deltaX`、`deltaY` | wheel | 滚动量（`deltaY` 为正表示向下） |
| `key`、`code` | keyboard | 键名与物理扫描码名 |
| `repeat`、`ctrl`、`shift`、`alt`、`meta` | keyboard | 修饰键 |
| `target` | pointer、wheel | 该点下最深的元素，表示为类 CSS 描述符（`div#main.card`），可能缺省 |

## 诊断

为了让 HTML/CSS/绘制流水线在没有 GPU 的情况下也可测试，窗口句柄暴露只读钩子：

```ts
win.computedStyle(selector, property)  // e.g. ("#main", "width") -> "60%"
win.queryCount(selector)               // 匹配元素的数量
win.getBoundingClientRect(selector)    // { x, y, width, height }（边框盒，含变换）
win.documentTree()                     // 序列化的 DOM（调试用）
win.layoutTree()                       // 序列化的布局盒（调试用）
win.paintCount()                       // 显示列表中图形的数量
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

它们被 `tests/e2e/gui-*.test.ts` 用来断言解析、选择器匹配、特异性、继承、
`!important` 与布局几何。它们在之后仍可用于调试。

## 多窗口

多窗口由对象模型自然得出：`createWindow` 返回一个原生对象句柄；每个句柄
拥有自己的 `SDL_Window`/GPU 表面与自己的 DOM 树。`run()` 驱动一个共享主循环，
它每帧 tick 所有窗口，并在最后一个窗口关闭时退出。