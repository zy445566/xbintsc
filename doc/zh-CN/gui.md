# xbintsc GUI 扩展（自托管 HTML/CSS 渲染器）

> 语言 / Language：[English](../gui.md) | **简体中文**

状态：**M11** —— 功能（M1–M11）已全部完成：HTML 解析、CSS 选择器匹配、层叠（cascade）、计算样式与布局（块级、行内、Flexbox 与 CSS 定位）均已就位，引擎还会**绘制**：它构建一个由矩形、图像与排版后的文本 run 组成的显示列表，并通过 SDL_GPU 渲染。输入事件会经过命中测试并投递给原生 TS 处理器，`:hover`/`:focus` 会被动态匹配，`<img>` 依据其固有尺寸确定大小并从纹理绘制，CSS transition 会为绘制属性做动画。**M8** 增加了交互式 DOM：具有稳定标识的元素句柄、变更操作（`appendChild`、`textContent`、`classList`、`style` 等）以及支持捕获/冒泡的元素级事件。**M9** 会提前编译 `<script>` 主体（内联与 `<script src>`）——参见 `gui-scripts.md`。**M10** 增加 `requestAnimationFrame` 以及若干 DOM 辅助方法。**M11** 增加 `position: relative`/`absolute`/`fixed` 及其偏移，`examples/gui/pelican-bike`（一个可玩的 2D 游戏）就构建在它之上。本文档记录了一个跨平台 GUI 扩展的锁定决策、架构、里程碑计划与当前进度；该扩展使用自有的 GPU 加速引擎渲染 HTML/CSS UI。

## 目标

1. **跨平台 GUI**：由 xbintsc 把 TypeScript 编译为原生二进制：macOS、Linux、Windows。
2. **HTML5/CSS 渲染**，使用**自研**引擎（解析器、层叠、布局、绘制），而非系统 WebView 或嵌入式浏览器。
3. **GPU 加速**——硬件光栅化/合成是硬性要求，而不是优化项。
4. **多窗口。**
5. 引擎不得损害 xbintsc 作为二进制编译器的本质：核心编译器、词法器、解析器、绑定器与代码生成器必须保持平台无关，绝不滋生 GUI 分支。

## 非目标（目前）

- 执行**运行时**页面 `<script>`（通过网络获取或动态创建的脚本）。**编译期**脚本由 xbintsc AOT 编译，并且确实会运行——参见 `gui-scripts.md`。逻辑也可以写在通过 `xt_call_with_this` 回调的原生 TS 中。
- JS 引擎（QuickJS/V8/...）。明确不在范围内；没有运行时解释器/JIT，因此 `<script>` 主体会提前编译。
- 完整的 Web 兼容性 / 浏览器。我们实现一个实用的 HTML/CSS 子集。

## 锁定决策

| # | 决策 |
| --- | --- |
| 1 | **没有页面 JS 引擎。** 行为由原生 TS 实现，通过 `xt_call_with_this` 回调。`<script>` 主体由 xbintsc 自身 **AOT 编译**（无解释器）——参见 `gui-scripts.md`。 |
| 2 | 允许使用第三方**底层**库（GPU 后端、文本整形、图像解码）。HTML/CSS 解析 + 布局 + 绘制调度均为自研。 |
| 3 | **GPU 加速是强制要求。** |
| 4 | 引擎**非自托管**（它是 C/C++，不是 TS），但不得影响编译器的平台无关设计。以按平台预构建的归档形式交付，通过 `nativeObjects` 链接。 |
| 5 | **事件循环是通用的**：运行时暴露一个通用主循环钩子与一个轮询原语；任何 GUI 特定的东西都不会进入 `runtime/`。 |
| 6 | 核心对象模型支持**多窗口**。 |
| 7 | 第三方底层库**静态链接进 `gui.a`**，使发布版保持自包含；链接时只额外加入操作系统框架。 |

## 架构

```
              TypeScript（由 xbintsc 编译为原生代码）
                │  import { createWindow, run } from "gui"
                ▼
        gui 扩展绑定  (src/extensions/gui)
                │  xt_gui_* 符号  （统一的 (argc, argv) ABI）
                ▼
             gui.a                  ── 自研引擎（C/C++）
   ┌───────────────┬──────────────────┬───────────────────┐
   ▼               ▼                  ▼                   ▼
 HTML 解析器    CSS 层叠 +         布局                GPU 合成器
 （子集）       选择器匹配     （块级/行内/flex）  （SDL_GPU: Metal/
                                                      Vulkan/D3D12）
                │
                ▼
        窗口 + 输入（SDL3）── 多窗口
        文本（HarfBuzz + FreeType）── 字形图集 / SDF
        图像（stb_image）
```

引擎从不与编译器通信。编译器只看到一个带有 `nativeObjects()`、`linkerFlags()` 和 `modules()` 的 `Extension`。

## 技术栈（已确认）

| 关注点 | 选择 | 理由 |
| --- | --- | --- |
| 窗口 + 输入 + 多窗口 | **SDL3** | 跨平台窗口、HiDPI、IME、剪贴板、拖放、关闭/隐藏事件 |
| GPU | **SDL_GPU**（SDL3） | 一条覆盖 Metal / Vulkan / D3D12 的渲染路径；避免三套后端 |
| 文本整形 | **HarfBuzz** | 正确处理复杂文种的整形 |
| 字形光栅化 | **FreeType** | 字形轮廓 → GPU 图集 / SDF |
| 图像 | **stb_image**（vendored 头文件） | 起步阶段用单头文件 |

固定版本：SDL3 `release-3.2.10`、FreeType `2.13.3`、HarfBuzz `10.1.0`
（可用 `SDL3_TAG` / `FREETYPE_VERSION` / `HARFBUZZ_VERSION` 覆盖）。

如果否决 SDL3，备选是 **GLFW + OpenGL 3.3**（更简单，但 OpenGL 在 macOS 上
已弃用，且不提供现代 GPU 抽象）。如果引擎用 Rust 编写，则用 **wgpu-native**。

> 这些 vendored 库会**静态链接进 `gui.a`**，因此发布的 xbintsc 保持
> “下载即用”；`linkerFlags()` 只添加操作系统框架。

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
`"metal"` 等，由 SDL 挑选设备支持的最佳后端）。

在 Windows 上，GUI 程序会被链接为 **Windows 子系统**可执行文件，因此启动时不会闪出控制台
窗口；这也意味着从资源管理器启动时 stdout/stderr 无处可去，调试时可在
`xbintsc.config.json` 里设置 `app.console: true` 保留控制台子系统。

触发的事件：
`ready`（首个呈现帧之后）、`load`、`close`，以及输入事件
`mousemove`、`mousedown`、`mouseup`、`click`、`wheel`、`keydown`、`keyup`。

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

### 已实现的 HTML/CSS 子集（M3a）

**HTML 解析器**（`runtime/ext_gui/dom.{h,cpp}`）：标签/属性/文本，注释与
doctype 跳过，实体解码（命名 + 十进制/十六进制），空元素，原始文本元素
（`<style>`/`<script>`），以及常见的隐式闭合规则（`li`、`dt`/`dd`、`option`、
`p`、标题、表格单元格/行）。

**CSS 解析器**（`runtime/ext_gui/css.{h,cpp}`）：注释与 at-rule 暂时跳过，
支持多选择器的规则以及声明。选择器：类型、`.class`、`#id`、属性
（`=`、`~=`、`|=`、`^=`、`$=`、`*=`）、四种组合器（后代、子、相邻兄弟与
通用兄弟）以及伪类 `:first-child`、`:last-child`、`:only-child`、`:empty`、
`:root`、`:not(...)`、`:nth-child(an+b)`、`:disabled`、`:checked`，再加上
有状态的 `:hover` 与 `:focus`（见 *已实现的输入*）。值：长度
（`px`、`%`、`em`、`rem`、`vw`、`vh`、`pt`、`pc`、`in`、`cm`、`mm`、`q`）、
颜色（十六进制、`rgb()`/`rgba()`、一个命名子集）、数字、关键字与简写
（`margin`/`padding`/`border`/`flex`）。

**层叠与计算样式**（`runtime/ext_gui/style.{h,cpp}`）：一份小的内置 UA
样式表，作者规则按 `(!important, 特异性, 源码顺序)` 排序，然后是内联
`style=""`（特异性最高，但非 `!important` 的内联会输给 `!important`），
再加上文本属性的 CSS 继承。相对长度保持未解析状态直到布局阶段，`font-size`
除外（相对*父级*字号解析）以及 `line-height`。

### 诊断

为了让 HTML/CSS/绘制流水线在没有 GPU 的情况下也可测试，窗口句柄暴露只读钩子：

```ts
win.computedStyle(selector, property)  // e.g. ("#main", "width") -> "60%"
win.queryCount(selector)               // 匹配元素的数量
win.getBoundingClientRect(selector)    // { x, y, width, height }（边框盒）
win.documentTree()                     // 序列化的 DOM（调试用）
win.layoutTree()                       // 序列化的布局盒（调试用）
win.paintCount()                       // 显示列表中图形的数量
win.paintList()                        // 序列化的显示列表（调试用）
win.measureText(text, fontSize?, family?)  // 整形后的推进宽度（像素）
win.fontMetrics(fontSize?, family?)    // { ascent, descent, lineHeight, ready }
win.hitTest(x, y)                      // 最深的元素描述符，或 ""
win.sendEvent(type, options?)          // 合成输入（测试用）
win.advance(ms)                        // 推进 CSS transition 时钟（测试用）
```

它们被 `tests/e2e/gui-*.test.ts` 用来断言解析、选择器匹配、特异性、继承、
`!important` 与布局几何。它们在之后仍可用于调试。

### 交互式 DOM（M8）

`win.document` 返回文档句柄；元素句柄具有稳定标识，并提供可读写的属性、
特性（attribute）、遍历与几何信息：

```ts
const doc = win.document;
const box = doc.querySelector("#box");
const inner = doc.querySelector("#inner");
inner.textContent = "hi";              // 写入
inner.classList.add("hot");
inner.style.setProperty("color", "#0f0");
inner.setAttribute("data-role", "lead");
console.log(inner.id, inner.tagName, doc.querySelector("#box") === box);

const created = doc.createElement("div");
created.textContent = "added";
box.appendChild(created);
box.removeChild(created);

inner.addEventListener("click", (e) => console.log(e.target.id, e.currentTarget.id));
inner.click();                          // 在该元素处合成一次点击
console.log(box.offsetWidth, box.offsetHeight);   // 取整后的边框盒
console.log(box.contains(inner));                 // 后代判断
```

变更操作会把文档标记为脏；引擎会延迟地（在下一次读取或下一帧之前）重新计算
样式并重新布局。`inner = …`/`innerHTML = …` **不会**执行脚本。元素事件支持
捕获与冒泡阶段、`stopPropagation`、`once`，并向上冒泡到 `document`/`window`。
旧式 `win.on(type, fn)` 负载保留其**字符串** `e.target`（`div#id.class`）；
元素 `Event.target` 是一个句柄，其描述符与同一字符串匹配。

### AOT 脚本（M9）

导入一个包含内联 `<script lang="ts">` 主体的 `.html` 文件；加载器会把每个
主体编译为一个原生函数，`win.loadHTML(page)` 会在文档解析完成后运行它们
（没有 JavaScript 引擎，没有运行时 `eval`）：

```ts
import { createWindow, run } from "gui";
import page from "./page.html";

const win = createWindow({ title: "counter", width: 320, height: 240 });
win.loadHTML(page);
run();
```

```html
<button id="b">0</button>
<script lang="ts">
  const b = document.getElementById("b");
  let n = 0;
  b.addEventListener("click", () => { b.textContent = String(++n); });
</script>
```

这两个参数（`window`、`document`）只是普通的函数参数，因此脚本的局部变量
无需任何全局对象机制。外部脚本同样可用：`<script src="./counter.ts">` 会在
编译期读取，其 import 会被提升（重写为从 HTML 文件解析），其主体以同样方式
包装——因此脚本模块可以 `import` 辅助函数，同时仍能看到 `document`。脚本是
**编译期资产**：运行时创建的 HTML（`innerHTML`、网络获取）永不执行，`src`
URL（`https://…`、`data:…`）会被忽略。一切都按文档顺序在解析后运行（实际上
是 deferred）。完整设计参见 `gui-scripts.md`。

### 动画帧（M10）

`requestAnimationFrame` 在下一帧运行一次回调；回调接收帧时间戳（毫秒），
并且可以修改 DOM，DOM 会在同一帧内重新计算样式并重绘。在回调内部再次排队
即可实现动画：

```ts
const win = createWindow({ title: "anim", width: 320, height: 240 });
let n = 0;
const tick = (t: number) => {
  win.document.getElementById("label").textContent = String(n++);
  if (n < 120) win.requestAnimationFrame(tick); // 返回 id；cancelAnimationFrame(id) 可取消
};
win.on("ready", () => win.requestAnimationFrame(tick));
win.loadHTML("<div id='label'>0</div>");
run();
```

### 已实现的布局（M3）

`runtime/ext_gui/layout.{h,cpp}` 把带样式的 DOM 转换为具有绝对（相对视口）
几何信息的 `LayoutBox` 树：

- **块级流** —— 块级子元素垂直堆叠（暂不支持外边距折叠）；`display: none`
  不生成盒；`width: auto` 填满包含块，`height: auto` 包裹内容。盒模型
  （margin/padding/border）会被解析，包括相对包含块宽度的百分比。
- **行内流** —— 连续的行内级子元素构成一个匿名行内格式化上下文，采用贪心、
  基于单词的断行、`text-align` 与 `line-height`。行内元素获得其后代几何信息
  的并集；`display: inline-block` 以 shrink-to-fit 宽度原子化布局。每个文本
  片段会记住它覆盖的 run，以便绘制时对它整形。文本使用 HarfBuzz/FreeType
  技术栈测量（见 *已实现的文本*）。
- **Flexbox** —— 单行 `row`/`column`（以及 `-reverse` 变体），支持 `gap`、
  `flex-basis`/`flex-grow`/`flex-shrink`、`justify-content` 与 `align-items`
  （当交叉轴尺寸确定时包括 `stretch`）。flex 项的外边距参与其外框尺寸，因此
  负的 `margin-top` 会把该项上移，而不是被忽略。
- **定位** —— `position: relative` 会按 `top`/`right`/`bottom`/`left` 偏移该盒子
  及其子树，但不改变它在流中占用的空间；`position: absolute`（以及针对视口解析
  的 `fixed`）使其脱离文档流，并相对最近的已定位祖先的 padding box 定位（没有
  这样的祖先时则相对视口）。`auto` 的 inset 在该轴上保留静态位置；宽度 `auto`
  时使用 shrink-to-fit，除非 `left` 与 `right` 同时给出（此时填满两者之间的空间）；
  百分比相对包含块解析。目前还没有 `z-index` 层叠，绘制仍按文档顺序进行。

尚未实现：外边距折叠、多行 flex 换行、`z-index` 层叠、`overflow` 裁剪与浮动。

**Document**（`runtime/ext_gui/document.{h,cpp}`）：持有 DOM 树，把 `<style>`
文本汇总为一份样式表，为某个视口计算样式与布局，并提供
`querySelector`/`querySelectorAll`/`styleOf`/`boxOf`。

### 已实现的绘制（M4a/M4b）

`runtime/ext_gui/paint.{h,cpp}` 按画家顺序遍历布局树，生成一个与后端无关的
`DisplayList`，其中有两个并列的列表：**矩形**（背景与四条实心边框，带
`border-radius`）与**文本 run**（每个携带其文本、颜色与已解析的 `FontSpec`）。
把它们分开可以让渲染器先绘制所有矩形，再在其上绘制所有文本，每个列表一次
绘制调用。`DisplayList::dump()` 为 `paintList()`/`paintCount()` 提供数据。

`runtime/ext_gui/renderer.{h,cpp}` 把该列表转换为每个窗口两个批处理顶点缓冲
（一个用于图形，一个用于字形四边形），通过两条 SDL_GPU 图形管线绘制：

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
  构建但在上游修复前不可用。
- 图形片元着色器中的**圆角矩形距离场**提供抗锯齿填充；顶点携带
  `position`、`local`、`half extents`、`radius` 与颜色，一个视口尺寸的
  push constant 负责投影。启用 alpha 混合。
- 文本管线采样一张**单一共享的灰度字形图集**（`R8_UNORM`，2048²，
  shelf 打包，LINEAR 过滤），把每个字形绘制为带纹理的四边形
  （`position`、`uv`、颜色），按覆盖率调制 alpha。
- 只有在文档或视口变化（`geometry.dirty`）时才上传几何数据，因此稳态帧
  只需绑定并绘制。

窗口背景（`setBackground`）是渲染通道的清除色。

M4 仍需完成：渐变。

### 已实现的输入（M5）

`LayoutTree::hitTest` 返回包含某点的最深盒（先探测靠后的兄弟，使最上层元素
胜出），`xt_dom_describe` 把元素转换为事件负载携带的 `div#id.class` 描述符。

- SDL 指针/滚轮/键盘事件会被路由到所属窗口，进行命中测试，并投递给用 `on`
  注册的处理器。`mousedown` 还会移动焦点。
- `:hover` 匹配悬停元素**及其祖先**（因此悬停子元素会点亮其父元素）；
  `:focus` 匹配聚焦元素。当任一改变时，`XtDocument::setHover`/`setFocus`
  重新计算样式与布局，并把窗口几何标记为脏，因此改动会在下一帧绘制。
- `win.hitTest(x, y)` 与 `win.sendEvent(type, options)` 暴露命中测试与合成
  输入，使整条路径可无头测试（e2e 套件在没有真实鼠标的情况下驱动点击、滚轮
  与按键）。

完整输入仍需完成：文本选择、拖拽、IME 与剪贴板。

### 已实现的图像（M6a）

`runtime/ext_gui/image.{h,cpp}` 封装 vendored **stb_image** 头文件，把
PNG/JPEG/BMP/GIF/TGA 解码为 RGBA8，并带有解码缓存与仅头文件的尺寸缓存
（`stbi_info`）。路径接受 `file://` 前缀与百分号编码。

- `<img>` 是一个替换元素（`display: inline-block`）：设置了 CSS 尺寸时布局
  采用该尺寸，否则采用固有像素尺寸；当只约束一个轴时保持宽高比。构建盒树时
  从文件头（无需完整解码）读取固有尺寸。
- 绘制时为每个 `<img>` 生成一个 `PaintImage`；渲染器把每个唯一 `src` 解码/
  上传到 RGBA 纹理（按路径缓存）并绘制带纹理的四边形，把共享同一纹理的连续
  四边形批处理。

图像仍需完成：CSS `background-image: url(...)`、`data:` URI、`object-fit`
与九宫格边框。

### 已实现的 transition（M6b）

`XtDocument` 在*目标*计算样式（当前 `:hover`/`:focus` 状态下的层叠结果）与
用于布局和绘制的*显示*样式之间运行 CSS transition。当状态变化改变了某个参与
transition 的属性时，会记录一个正在运行的 transition，并在每帧重新应用直到
结束；引擎用真实帧间隔推进时钟（`XtDocument::advance`），`win.advance(ms)`
让测试可以确定性地推进。

- 支持的属性：`background-color`、`color`、`border-color`、
  `border-radius`；`transition: all` 覆盖它们。影响结构/布局的属性暂不做
  动画（那会每帧重新布局）。
- `transition` 简写与 `transition-property` / `-duration` / `-delay` /
  `-timing-function` 长写都会被解析；时间值接受 `s` 与 `ms`；缓动函数为
  `linear`、`ease`、`ease-in`、`ease-out` 与 `ease-in-out`（`ease` 是
  smoothstep 近似）。
- 在动画中途重定向会从当前插值启动一个新 transition，因此反转悬停会从它
  当前所在位置平滑动画。
- `computedStyle()` 报告显示（已插值）的值，因此 transition 在测试中可直接
  观察。

动画仍需完成：`@keyframes` 动画与 `cubic-bezier(...)`。

### 已实现的文本技术栈（M4b）

`runtime/ext_gui/text.{h,cpp}` 封装 **HarfBuzz**（整形）与 **FreeType**（度量
与最终光栅化）。两者都静态构建，并由 `scripts/build-gui.ts` 链接进 `gui.a`。

- 字体从常见系统路径解析（macOS 上的 Helvetica/Arial、Linux 上的
  DejaVu/Liberation、Windows 上的 Segoe UI/Arial），可用 `XT_GUI_FONT`
  （以及 `XT_GUI_FONT_MONO`）覆盖，并按 `(字体类别, 字号)` 缓存。目前只使用
  常规直立字面；字重/斜体选择是后续细化。
- `xt_text_measure_width` 用 HarfBuzz 对 run 整形（因此字距调整与连字会生效），
  `xt_text_metrics` 返回 FreeType 的 ascent / descent / 正常行高。当找不到
  字体文件时，模块回退到确定性的逐字节近似，因此布局仍可工作。
- `xt_text_shape_run` 返回定位后的字形，`xt_text_rasterize` 渲染 8 位位图；
  渲染器把它们打包进图集。在 HiDPI 显示器上，字形以
  `font_size * SDL_GetWindowPixelDensity` 光栅化，而四边形以逻辑像素定位，
  因此文本保持清晰。
- 布局使用这些真实度量进行文本宽度、断行与 `line-height: normal` 计算；
  `measureText`/`fontMetrics` 把它们暴露给测试。

多窗口由对象模型自然得出：`createWindow` 返回一个原生对象句柄；每个句柄
拥有自己的 `SDL_Window`/GPU 表面与自己的 DOM 树。`run()` 驱动一个共享主循环，
它每帧 tick 所有窗口，并在最后一个窗口关闭时退出。

## 事件循环集成

运行时改动（已落地）：

- `int xt_loop_poll(int timeout_ms)` —— 一次 reactor 迭代；`0` 非阻塞轮询，
  `< 0` 阻塞。
- `void xt_loop_set_main(xt_main_loop_fn fn)` —— 宿主可以接管主循环。
  `xt_run_event_loop()` 会委托给它；生成的 `main` 不变。
- `xt_loop_set_main(NULL)` 恢复默认的 `select(2)` 循环。

GUI 引擎既可以通过 `xt_loop_set_main` 注册自己的循环，也可以暴露显式的
`run()`；**M2 使用显式的 `run()`**，因此窗口是 TypeScript 程序可以控制的
普通原生调用。每次 tick 它都会：

1. 为每个窗口泵送 SDL 窗口/输入事件，
2. 当显示列表变化时构建/上传它，并渲染每个打开的窗口（清除通道 + 图形几何
   + 文本几何），
3. 调用 `xt_loop_poll(0)` 与 `xt_drain_microtasks()`，使 socket/定时器与
   `await` 续体持续取得进展，
4. 重复直到所有窗口关闭或调用 `quit()`。

`xt_loop_set_main` 钩子仍保留给希望自行掌控循环的宿主。

这使网络 I/O、定时器与 `await` 在 GUI 程序内部继续工作。

## 原生 ↔ TS 桥接

- **TS → 引擎**：直接的 `xt_gui_*` 调用 / 窗口方法。
- **引擎 → TS**：`xt_call_with_this(fn, thisValue, argc, argv)`，其中函数值
  从 TS 捕获（例如用 `win.on(...)` 注册的事件处理器）。
- **未来的页面→原生 RPC**：在没有页面 JS 时不需要；原生 TS 就是控制器。
  如果日后加入声明式层，它将使用相同的 `xt_call_*` 入口。

## 打包与构建

- 源码位于 `runtime/ext_gui/`（C/C++），外加 `vendor/` 下的 vendored 库
  （被 gitignore；按需获取）。
- `npm run gui`（`scripts/build-gui.ts`）获取固定版本的 SDL3（`SDL3_TAG`，
  默认 `release-3.2.10`），构建静态 SDL3，获取并构建静态 FreeType
  （`FREETYPE_VERSION`）与 HarfBuzz（`HARFBUZZ_VERSION`），编译引擎，并把
  所有内容合并到 `runtime/lib/<os>-<arch>/gui.a`（Windows 上 MSVC ABI 则为
  `gui.lib`），与 `core.a` 采用相同约定，静态打包三者。合并使用 macOS 上的
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

### 运行示例

```sh
npm run runtime      # runtime 改动后重建 core.a
npm run gui          # 构建 runtime/lib/<os>-<arch>/gui.a（首次会获取 SDL3）
xbintsc run examples/gui/hello.ts --ext gui
```

设置 `XT_GUI_AUTOCLOSE_MS=<n>` 可在 `n` 毫秒后关闭所有窗口，e2e 测试
（`tests/e2e/gui-*.test.ts`）用它来无头运行。

这里有两个示例：`hello.ts`（HTML/CSS + 响应式计数器）与
[`pelican-bike/`](../examples/gui/pelican-bike)（一个可玩的 2D 游戏，使用分层
定位、CSS 像素精灵、`requestAnimationFrame` 与键盘输入）——操作方式与它输出的
几何报告见该目录的 README。

这些测试套件在 Windows 上默认跳过（该平台会构建归档，但 CI 未验证运行）。
如果本机有 `gui.lib` 与可用的 GPU，可用 `xbintsc_GUI_TESTS=1` 选择启用：

```bat
set xbintsc_GUI_TESTS=1
npx vitest run tests/e2e/gui-layout.test.ts tests/e2e/gui-example.test.ts
```

在无头 Linux 机器上，安装 SDL3 构建头文件，并在 Xvfb 与软件 Vulkan 驱动下
运行：

```sh
sudo apt-get install -y clang cmake libx11-dev libxext-dev libxrandr-dev \
  libxcursor-dev libxi-dev libxinerama-dev libxfixes-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols libdecor-0-dev libasound2-dev libpulse-dev \
  libdbus-1-dev libudev-dev libdrm-dev libgbm-dev libgl1-mesa-dev \
  libegl1-mesa-dev libvulkan-dev mesa-vulkan-drivers xvfb
npm run runtime && npm run gui
xvfb-run -a --server-args="-screen 0 1280x720x24" \
  npx tsx src/cli/main.ts run examples/gui/hello.ts --ext gui
```

这些就是 CI 的 `compile-examples` job 安装的包。默认使用 X11 后端；Wayland
也已启用，但尚未实际演练。

## 里程碑

1. **M1 — 基础** ✅
   - 运行时中的通用 `xt_loop_poll` / `xt_loop_set_main`。
   - `gui` 扩展骨架 + 通用 CLI 扩展注册。
2. **M2 — 窗口 + GPU 清除** ✅
   - SDL3 窗口、SDL_GPU swapchain、多窗口、主循环集成。
   - `createWindow` / `run` / `quit` + `on`/`off` 窗口方法端到端可用
     （`runtime/ext_gui/`、`scripts/build-gui.ts`）。
3. **M3 — HTML/CSS 子集** ✅
   - **M3a — 解析 + 层叠** ✅ HTML 解析器、DOM 树、CSS 解析器、选择器匹配、
     UA/作者/内联层叠、继承、计算样式
     （`dom.*`、`css.*`、`style.*`、`document.*`）。
   - **M3b — 布局** ✅ 块级/行内流 + Flexbox（`layout.*`）、
     `getBoundingClientRect`/`layoutTree`。
4. **M4 — 绘制 + 文本 + 显示列表**
   - **M4a — 显示列表 + GPU 图形** ✅ 背景/边框显示列表、圆角矩形 SDL_GPU
     管线（`paint.*`、`renderer.*`）。
   - **M4b-1 — 文本技术栈 + 度量** ✅ HarfBuzz + FreeType 链接进 `gui.a`、
     字体解析/缓存、供布局使用的基于整形的文本度量
     （`text.*`、`measureText`/`fontMetrics`）。
   - **M4b-2 — 字形渲染** ✅ FreeType 光栅化、共享的 shelf 打包字形图集、
     显示列表中的带纹理文本四边形与感知 HiDPI 的光栅缩放。渐变仍待完成。
5. **M5 — 输入 + 事件** ✅
   - 命中测试（`LayoutTree::hitTest`）、带负载投递给 TS 处理器的指针/滚轮/
     键盘事件、`:hover`/`:focus` 有状态匹配与重算样式
     （`css.*`、`style.*`、`document.*`、`gui.cpp`），以及
     `hitTest`/`sendEvent` 测试钩子。
6. **M6 — 图像，然后是 CSS transition/动画**
   - **M6a — 图像** ✅ stb_image 解码、`<img>` 替换元素布局、按文件 GPU 纹理
     与带纹理四边形（`image.*`、`paint.*`、`renderer.*`）。
   - **M6b — transition/动画** ✅ `transition` 简写 + 长写、对
     `background-color`/`color`/`border-color`/`border-radius` 的动画、重定向
     与 `win.advance(ms)`。`@keyframes` 仍待完成。
7. **M7 — CI 与发布** ✅（Linux/macOS 构建） / 🚧（运行 + Windows）
   - `compile-examples` 在 Linux 与 macOS 上构建 `gui.a`（必需），然后在
     Linux 上的 Xvfb + lavapipe 下运行示例与 `tests/e2e/gui-*.test.ts`（必需）。
     macOS 运行在确认 WindowServer 之前为试验性；它被保护起来，失败只记录
     日志而不标注运行。
   - `package` job 在组装发布之前构建 `gui.a`，因此它会随现有 runtime 归档
     一起发布（`package-release` 复制整个 `runtime/`）。
   - `vendor/`（SDL3/FreeType/HarfBuzz，缓慢的部分）按 OS/arch 缓存，以
     `scripts/build-gui.ts` 为键。
   - Windows（构建 + 运行）保持试验性，直到验证 MSVC 兼容的 `gui.lib` 与
     D3D12/DXIL 着色器路径；失败只记录日志而不标注运行（见 *待决问题*）。
8. **M8 — 交互式 DOM** ✅
   - 具有稳定标识的元素/文档句柄、通过运行时访问器读写属性、遍历/特性/查询、
     带有延迟重算样式/重新布局的变更
     （`dom_api.*`、`document.*`、`dom.*`、`gui.cpp`、`window.cpp`）、具有
     捕获 + 冒泡阶段与 `stopPropagation` 的元素事件，以及窗口级 `e.target`
     兼容性。无编译器改动。参见 `gui-scripts.md`。
9. **M9 — AOT `<script>`** ✅
   - **M9a** ✅ —— 通用 `Extension.assetLoaders` 钩子 + bundler 集成。
   - **M9b** ✅ —— gui `.html` 资产加载器（`src/extensions/gui/html.ts`）：
     内联主体被包装进 `__xt_script_<hash>(window, document)` 函数，通过
     `__registerScript` 内置函数注册，并替换为
     `<script data-xt-id="<hash>">` 标记；`win.loadHTML` 在解析后运行匹配的
     函数并触发 `DOMContentLoaded` 然后是 `load`。
   - **M9c** ✅ —— `<script src>` 文件相对 HTML 读取，其顶层 import 被提升
     （说明符从 HTML 目录重写），其主体被包装/注册；缺失文件与 import 绑定
     冲突会成为诊断。一切都按文档顺序运行。
10. **M10 — 打磨** ✅
    - `win.requestAnimationFrame(fn)` / `win.cancelAnimationFrame(id)`；回调
      在每帧顶部以帧时间戳运行，并可修改 DOM
      （`gui.cpp`、`window.cpp`、`gui_engine.h`）。
    - `Element.offsetWidth` / `offsetHeight`（取整后的边框盒，会冲刷待处理
      的变更）与 `Element.contains(other)`（`dom_api.cpp`）。
11. **M11 — CSS 定位** ✅
    - `position: relative` / `absolute` / `fixed` 与 `top`/`right`/`bottom`/
      `left`，相对最近的已定位祖先解析（没有时相对视口）；`auto` inset 保留
      静态位置；脱离文档流的 `width: auto` 使用 shrink-to-fit，两个水平 inset
      都给出时则填满可用宽度（`layout_flow.cpp`、`layout.h`）。
    - flex 算法现在会计入项的外边距，负外边距参与布局而不再被丢弃
      （`layout_flex.cpp`）。
    - `examples/gui/pelican-bike` 基于它实现，e2e 覆盖见
      `tests/e2e/gui-layout.test.ts`。

## 进度日志

- **M1** ✅ 通用 `xt_loop_poll`/`xt_loop_set_main`；`gui` 扩展骨架。
- **M2** ✅ SDL3 窗口 + SDL_GPU 清除、多窗口、`createWindow`/`run`。
- **M3a** ✅ HTML 解析器（`dom.*`）、CSS 解析器/匹配器（`css.*`）、层叠与
  计算样式（`style.*`）、文档模型（`document.*`）、
  `computedStyle`/`queryCount`/`documentTree`、e2e 覆盖。
- **M3b** ✅ 布局（`layout.*`）：块级流、带断行的行内格式化上下文、单行
  Flexbox、`getBoundingClientRect`/`layoutTree`、e2e 覆盖。
- **M4a** ✅ 显示列表（`paint.*`）与带 MSL 圆角矩形管线的 SDL_GPU 2D 渲染器
  （`renderer.*`）、`paintList`/`paintCount`、e2e 覆盖。
- **M4b-1** ✅ FreeType + HarfBuzz 获取/构建/合并进 `gui.a`、带字体解析的文本
  模块（`text.*`）、HarfBuzz 整形与 FreeType 度量、布局中的真实文本度量、
  `measureText`/`fontMetrics`、e2e 覆盖。
- **M4b-2** ✅ `renderer.*` 中的字形图集 + 带纹理文本管线、`paint.*` 中的
  整形文本 run、`text.*` 中的 `xt_text_shape_run`/`xt_text_rasterize`、
  HiDPI 光栅缩放、e2e 覆盖。
- **M5** ✅ 命中测试 + 输入事件（`LayoutTree::hitTest`、`xt_dom_describe`、
  `xt_gui_dispatch_*`）、匹配器中的 `:hover`/`:focus` 与动态重算样式、
  `hitTest`/`sendEvent` 测试钩子、e2e 覆盖。
- **M6a** ✅ 图像解码（`image.*`、vendored stb_image）、布局中的 `<img>`
  固有尺寸、显示列表中的 `PaintImage`、按文件 RGBA 纹理与 `renderer.*` 中的
  图像管线、e2e 覆盖。
- **M6b** ✅ CSS transition（在 `style.*` 中解析、在 `document.*` 中维护动画
  时钟与 transition 状态、`advance`/`advance(ms)` 钩子、e2e 覆盖）。
- **M7** ✅ 在 CI 中于 Linux 与 macOS 上构建 `gui.a`（必需），并在 Linux 的
  Xvfb 下运行示例与 GUI e2e 套件；`package` 发布 `gui.a`，`vendor/` 被缓存，
  `ar -M` 合并在 GNU/Linux 与 macOS 上可用。macOS 运行与所有 Windows 在 CI
  中仍为试验性。
- **M8** ✅ 元素/文档句柄（`dom_api.*`）、带延迟重算样式/重新布局的 DOM 变更
  （`document.*`、`xt_gui_flush_dom`）、带捕获/冒泡与 `stopPropagation` 的
  元素事件分发（`dom_api.*`、`gui.cpp`），以及
  `tests/e2e/gui-*.test.ts` 中的 e2e 覆盖。
- **M9a** ✅ 扩展可以按文件扩展名注册资产加载器；
  `bundleModules`/`loadGraph` 在读取文件后查询它们。
  `tests/driver/modules.test.ts` 中的单元测试。
- **M9b** ✅ `import page from "./page.html"` 把内联 `<script lang="ts">` 主体
  编译为启动时注册、由 `win.loadHTML` 运行的 AOT 函数（在首次布局之前；
  `DOMContentLoaded` 然后是 `load`）。`tests/extensions/gui.test.ts` 中的单元
  测试，`tests/e2e/gui-*.test.ts` 中的 e2e。
- **M9c** ✅ 外部 `<script src>` 文件被读取，其 import 被提升（说明符从 HTML
  目录重写），其主体被包装/注册；缺失文件与 import 绑定冲突会成为诊断。
  加载器错误被 `loadGraph` 捕获并报告为构建错误。
- **M10** ✅ 窗口句柄上的 `requestAnimationFrame`/`cancelAnimationFrame`
  （回调在每帧布局之前以帧时间戳运行），以及元素句柄上的
  `offsetWidth`/`offsetHeight`/`contains`；e2e 覆盖在
  `tests/e2e/gui-*.test.ts` 中。
- **M11** ✅ `position: relative`/`absolute`/`fixed` 与 `top`/`right`/`bottom`/
  `left`。布局为每个盒子保留包含块（最近的已定位祖先的 padding box，且只在
  该祖先拥有最终尺寸后才发布，因此 `bottom`/`right` 精确）；绝对定位的盒子
  在正常流内容之后布局，因此不影响它们；实现了 shrink-to-fit / 填满可用宽度
  两种规则；flex 项现在会计入外边距。e2e 覆盖在
  `tests/e2e/gui-layout.test.ts`；`examples/gui/pelican-bike` 基于它实现。

## 已知问题

- **自动关闭时的偶发崩溃。** 设置 `XT_GUI_AUTOCLOSE_MS` 结束运行时，约五次中有两次会以原生
  访问违例（`0xC0000005`）退出，其余运行与正常退出无异。该问题只在**动画帧循环运行中**复现
  （静态文档可以正常关闭），且**不是示例游戏自身**的代码：没有任何异常信息，崩溃多发生在
  启动后一秒内、与截止时间无关，而提高 `XT_GC_THRESHOLD` 可以掩盖它，因此看起来是
  GC/析构竞态。交互式运行（不设自动关闭）可稳定运行数分钟。`tests/e2e/gui-example.test.ts`
  因此把该退出码视为可接受结果，行为断言放在不依赖 GPU 的
  `tests/e2e/gui-example-browser.test.ts` 中。
- **`display: none -> flex` 的重排。** 通过类名切换让隐藏元素显示在 flex 容器上时，也曾观察
  到同样的崩溃；`examples/gui/pelican-bike` 改用 `opacity` 显示游戏结束遮罩。
- **SVG：** 引擎不支持也不计划支持 SVG 元素。HTML 解析器只认识元素与属性，布局只认识盒子，
  绘制只认识矩形/文本/图像；`<svg>` 子树需要另一套几何与绘制路径。SVG *文件* 也只能走图像
  加载器（`<img src="logo.svg">`），而这需要 SVG 光栅化器（`stb_image` 不解码 SVG）。因此
  矢量美术要么以位图（PNG）形式提供，要么像 `examples/gui/pelican-bike` 那样用盒子绘制。

## 待决问题

- Linux 首发是只发布 X11、Wayland，还是两者都发布。（决定：先 X11，
  Wayland 随后。）
- Windows：`scripts/build-gui.ts` 会生成 MSVC 兼容的 `gui.lib`
  （COFF 对象 + `ar -M`/`llvm-ar`），但尚未在 CI 中验证，因此 Windows 步骤
  为试验性（job 保持绿色），`package` 会跳过它。它还需要一个带 D3D12/DXIL
  后端的 SDL3 构建（DXIL 需要 `dxc`）。
- **D3D12 着色器：** SPIR-V（Vulkan）与 DXIL（Direct3D 12）二进制块均已构建并
  内嵌，非 Metal 路径不再跳过几何绘制。但在 **SDL 3.2.10** 上 DXIL 仍不可用：
  其 D3D12 后端会拒绝任何着色器中声明了 uniform buffer 的图形管线，而引擎正是
  用这种方式传视口。SDL 升级后需重新确认。
