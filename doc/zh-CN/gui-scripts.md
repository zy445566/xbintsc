# GUI 扩展 —— DOM 句柄、事件与 AOT `<script>`

> 语言 / Language：[English](../gui-scripts.md) | **简体中文**

状态：**设计** —— 本文档规定 `gui` 扩展如何从只读的 HTML/CSS 渲染（M7）成长为
一个小的、*交互式* DOM，并带有**编译期（AOT）脚本**，同时不引入 JavaScript
引擎，也不让 GUI 的关注点泄漏进核心编译器。

它是里程碑 **M8**（DOM 对象模型 + 变更 + 元素事件）与 **M9**（AOT `<script>`
流水线）背后的计划。`gui.md` 中的锁定决策仍然成立；本文档只是细化“没有页面
JS”的含义。

## 动机

如今引擎渲染一棵 HTML/CSS 树，只暴露*窗口级*事件（`win.on("click", …)`）。
处理器接收到的 `e.target` 是一个 CSS 描述符字符串（`div#main.card`），DOM
是只读的（仅用于诊断），改变 UI 的唯一方式是 `win.loadHTML(...)`，它会重建
所有内容。

对于静态演示这已经足够，但接下来的明显问题 ——“我能写 `<script>` 并对特定
元素的点击做出反应吗？”—— 还没有答案。xbintsc 是一个**纯 AOT 编译器**，
**没有运行时解释器/JIT**，因此 `eval` 式的脚本执行是不可能的。运行脚本唯一
自洽的方式就是**提前编译它们**。

## 目标

1. **元素句柄** —— `document.querySelector(...)` 返回一个具有稳定标识的对象
   （同一元素 `a === b`）以及可读写的 DOM 属性（`id`、`className`、
   `textContent`、`innerHTML`、`style`、`classList`、特性、遍历、几何）。
2. **变更** —— `appendChild` / `removeChild` / `insertBefore` /
   `replaceChild` / `textContent=` / `innerHTML=` / `classList.*` /
   `style.setProperty` 更新树；引擎延迟地重新计算样式 + 重新布局并重绘。
3. **元素事件** —— `el.addEventListener(type, fn, options?)` 带捕获与冒泡
   阶段、`removeEventListener`、`dispatchEvent`、`el.click()`，事件对象带
   `target` / `currentTarget` / `preventDefault` / `stopPropagation`。
4. **AOT `<script>`** —— 用 TypeScript 编写的 `<script>` 主体在**编译期**被
   提取，由*现有* xbintsc 前端作为普通模块编译，并在文档解析后由引擎调用。
5. **零核心编译器耦合** —— 词法器/解析器/绑定器/代码生成器对 HTML 或 GUI
   一无所知。扩展只贡献模块/对象（以及 M9 新增的**资产加载器钩子**）。
6. **向后兼容** —— 现有程序与测试继续工作：`win.on(...)` 处理器继续接收
   *字符串* `e.target`。

## 非目标

- **JS 引擎**（QuickJS/V8/…）。仍明确不在范围内。没有运行时 `eval`，没有
  解释器，没有 JIT。
- **运行时的动态脚本。** 脚本是编译期资产。通过网络获取或在运行时产生的
  HTML **不会**执行其脚本。
- **安全沙箱 / 浏览器语义。** 没有源（origin）模型；脚本就是普通的原生 TS，
  可以 `import` 编译器允许的任何内容。这一点被记录为“不是浏览器”。
- **完整 DOM/BOM。** 我们实现 UI 所需的实用子集。

## 锁定决策（细化）

| # | 决策 |
| --- | --- |
| 1 | **没有页面 JS 引擎。** 行为由原生 TS 实现。`<script>` 主体由 xbintsc *AOT 编译*为原生代码，并通过 `xt_call_with_this` 调用——仍然没有解释器。 |
| 8 | **脚本是编译期资产。** HTML 资产加载器把内联 `<script>` 主体转换为已编译模块，并替换为 `data-xt-id` 标记；运行时只*调用*已编译的函数。 |
| 9 | **DOM 由引擎拥有；句柄是引擎对象。** 句柄使用与窗口句柄相同的共享原型对象模型；隐藏字段携带窗口引用、文档代（generation）与节点索引。 |
| 10 | **`window`/`document` 作为参数注入**，而非全局变量，因此不会向编译器添加全局对象机制。 |

## 架构

```
     <script>…</script>            编译期（bundler）
            │  gui .html 资产加载器
            ▼
   ┌──────────────────────┐        ┌───────────────────────────┐
   │ 转换 HTML 主体        │        │ 生成 __xt_script_<hash>    │  一个普通的
   │ → function           │        │       (window, document)  │  xbintsc
   │ <script data-xt-id>  │        │ + __registerScript(hash)  │  模块
   └──────────────────────┘        └───────────────────────────┘
            │                                   │
            ▼                                   ▼
     运行时 HTML 字符串                链接进二进制的原生代码
            │  win.loadHTML(html)
            ▼
   引擎解析 HTML，找到 `data-xt-id` 标记，在脚本注册表中查找每个 hash
   并调用 `fn(windowHandle, documentHandle)`
```

### 1. `Extension.assetLoaders`（唯一的新核心钩子）

核心编译器目前没有办法让扩展影响*文件如何加载*。M9 向
`src/extensions/registry.ts` 添加一个通用钩子：

```ts
interface AssetLoadResult {
  /** 替换 bundler 所看到的文件内容。 */
  moduleSource: string;
  /** 该资产依赖的额外文件（变化时重新编译）。 */
  dependencies?: string[];
}

interface Extension {
  // …现有字段…
  /** 可选：把导入的资产重写为 TS 模块。按扩展名索引。 */
  assetLoaders?(): Record<string, (path: string, source: string) => AssetLoadResult>;
}
```

bundler（`src/driver/bundler/graph.ts`）在 `readFileSync` 之后、解析*之前*
立即查询注册表。核心保持平台无关：它只知道“扩展可能把 `*.foo` 的字节转换为
TS”。`gui` 扩展注册一个 `.html` 加载器。因此，普通的
`import html from "./index.html"` 默认产生一个**字符串常量**，并且（当文件
包含 `<script>` 时）产生一个**有副作用的模块**，它注册已编译的脚本主体并
默认导出重写后的 HTML。

### 2. `.html` 转换

给定：

```html
<button id="b">0</button>
<script lang="ts">
  const b = document.getElementById("b");
  let n = 0;
  b.addEventListener("click", () => { b.textContent = String(++n); });
</script>
```

加载器生成如下形状的模块：

```ts
// generated
const __html = "<button id=\"b\">0</button>\n<script data-xt-id=\"a1b2…\"></script>";
function __xt_script_a1b2(window: any, document: any): void {
  const b = document.getElementById("b");
  let n = 0;
  b.addEventListener("click", () => { b.textContent = String(++n); });
}
__registerScript("a1b2…", __xt_script_a1b2);
export default __html;
```

- id 是主体内容的哈希（sha256，前 32 个十六进制字符）；主体被替换为一个空的
  标记元素，以便引擎能按文档顺序找到它。
- `window` 与 `document` 是**函数参数**，因此脚本的全局变量会作为普通局部
  变量解析 —— **无需编译器改动**，也没有全局对象。
- `__registerScript(id, fn)` 是一个新的运行时内置函数，它把闭包存进全局的
  `unordered_map<string, xt_value>`。
- 延迟脚本（`<script defer>`，或 M9c 中决定的所有内联脚本）在完整文档解析
  并计算首次布局之后按文档顺序运行。然后在 `window` 上依次触发
  `DOMContentLoaded` 再 `load`。

### 2b. 外部 `<script src>`

`<script src="./app.ts">` 相对 HTML 文件读取、解析并拆分：其顶层
`import`/`export … from` 语句被**提升**到生成的模块（相对说明符被重写以便
从 HTML 文件所在目录解析），其余语句被包装进脚本函数。这样外部脚本既能在
`loadHTML` 时运行，又能 `import` 其他模块。

两个限制被强制执行并以清晰的错误报告，而不是悄悄出错：

- 缺失的 `src` 文件会抛错（被转换为 bundler 诊断），并且
- 两个 `src` 脚本导入同一个本地绑定名会冲突；用户必须给其中一个起别名。
  （不冲突或完全相同的导入没问题。）

`src` URL（`https://…`、`data:…`、`//host`）保持原样 —— 它们不是编译期资产，
永不运行。

```html
<button id="b">0</button>
<script src="./counter.ts"></script>
```

```ts
// counter.ts
import { double } from "./helper";
const b = document.getElementById("b");
let n = 0;
b.addEventListener("click", () => { n = double(n) + 1; b.textContent = String(n); });
```

### 3. 运行时执行

`win.loadHTML(html)`：

1. 和今天一样解析文档，
2. 扫描 `data-xt-id` 标记，
3. 对每个标记调用 `registry[id](winHandle, docHandle)`，
4. 触发首次布局，然后触发 `DOMContentLoaded` 与 `load`。

因为调用发生在解析之后，脚本内部的 `document.getElementById(...)` 总能找到
其元素。`innerHTML = …` 永不执行内嵌脚本（标记是惰性的）。

## 元素句柄对象模型（M8）

句柄是普通的运行时对象（`xt_object_new_with_proto`），每种类型共享一个原型，
与窗口句柄完全一样：

| 隐藏字段 | 含义 |
| --- | --- |
| `__xt_gui_win` | 所属窗口对象（通过 `xt_gui_window_from_this` 解析） |
| `__xt_gui_gen` | 创建该句柄时的文档代 |
| `__xt_gui_node` | 窗口节点表中的索引（元素/文本句柄） |
| `__xt_gui_doc` | 单例 `document` 句柄为 `true` |

**标识是稳定的**：窗口维护 `node_order: vector<Node*>` 以及
`node_index`/`node_handles: map<Node*, …>`，因此两次查询同一元素返回*同一个*
对象值（`a === b`）。

**已分离的节点**在文档池中保持存活，因此跨 `remove()` 持有的句柄仍可解析
（墓碑语义）；代不匹配会使过期句柄变为无操作，而不是悬空指针。

### 访问器，而非字段

读写的 DOM 属性用 `xt_object_define_getter` / `xt_object_define_setter`
定义，它们已经支持原型继承且 `this` = 接收者。因此 `el.textContent = "x"`
与 `el.id = "y"` 可用，而值存放在 C++ 树中。`classList` 与 `style` 是持有
元素句柄反向引用的小对象（无需闭包/env；arena 永不释放，因此循环无害）。

### 已实现的表面（M8）

- `document`：`querySelector`、`querySelectorAll`、`getElementById`、
  `createElement`、`createTextNode`、`body`、`documentElement`、
  `addEventListener` / `removeEventListener` / `dispatchEvent`。
- node（元素/文本/文档）：`tagName` / `nodeName` / `nodeType`、`id`、
  `className`、`textContent` / `innerText`、`innerHTML`、`parentNode` /
  `parentElement`、`children` / `childNodes` / `childElementCount`、
  `firstElementChild` / `lastElementChild`、`nextElementSibling` /
  `previousElementSibling`、`isConnected`、`classList`、`style`。
- 特性：`getAttribute` / `setAttribute` / `hasAttribute` /
  `removeAttribute`。
- 查询：`querySelector` / `querySelectorAll` / `matches`（以子树为范围）。
- 变更：`appendChild` / `insertBefore` / `removeChild` / `replaceChild` /
  `remove` / `cloneNode(deep?)`。
- 几何/焦点：`getBoundingClientRect`、`focus`、`blur`、`click`。
- 事件：`addEventListener(type, fn, optionsOrCapture?)`、
  `removeEventListener`、`dispatchEvent`。

### 延迟重算样式 / 重新布局

每次变更都调用 `XtDocument::invalidate()` 并设置 `win->struct_dirty`。在任何
同步读取（`computedStyle`、`getBoundingClientRect`、`queryCount`、`layoutTree`、
`paintList`、`hitTest` 等）之前以及每帧之前，如果有任何待处理项，
`xt_gui_flush_dom()` 会**一次性**重算样式 + 重新布局。这使变更既廉价又批量，
同时让读取立即一致。

## 事件与冒泡

元素监听器存放在窗口上（`node_listeners`），以节点为键（而不是放在 `Node`
自身上，这样树保持聚焦于布局）。

分发：

1. 构建传播路径 `target → … → root`，
2. 创建一个 Event 对象（`type`、`target` = 节点句柄、`currentTarget`、
   `bubbles`、`defaultPrevented`、内部 `__stop` / `__stopImmediate`），
3. **捕获**阶段：root → target（用 `capture: true` 注册的监听器），
4. **冒泡**阶段：target → root，
5. 旧式窗口级 `win.on(type, fn)` 处理器**最后**运行。

`once` 监听器在被调用之前移除；`stopPropagation` 结束当前阶段链，
`stopImmediatePropagation` 还会跳过同一节点上靠后的监听器。`el.click()` 在
该节点合成 `click`（仅 DOM）；真实的指针/滚轮/键盘输入会分发 **DOM 事件与
旧式窗口负载两者**。

### 向后兼容

旧式窗口负载保留其**字符串** `e.target`
（`tests/e2e/gui-*.test.ts` 断言 `e.target === "div#inner"`）。只有*元素*
Event 的 `target` 是句柄，其描述符（`toString`）与同一 `div#id.class` 字符串
匹配。`make_dom_event` 有意**不**把旧式 `target` 字段复制到句柄上。

## 里程碑

- **M8 — DOM 对象模型 + 变更 + 元素事件** ✅
  - `runtime/ext_gui/dom_api.{h,cpp}`、`document.{h,cpp}`、`dom.{h,cpp}`、
    `gui_engine.h`、`gui.cpp`、`window.cpp`、e2e 覆盖。
  - 无编译器改动；可独立测试。
- **M9 — AOT `<script>`** ✅
  - **M9a** ✅ —— `src/extensions/registry.ts` 中的 `Extension.assetLoaders` +
    `src/driver/bundler/{graph,merge}.ts` 中的 bundler 集成；单元测试。
  - **M9b** ✅ —— gui `.html` 加载器（`src/extensions/gui/html.ts`）、
    `__registerScript` 内置函数、脚本注册表（`runtime/ext_gui/script.cpp`）、
    `loadHTML` 执行与 `DOMContentLoaded`/`load`；e2e 覆盖。
  - **M9c** ✅ —— 读取 `<script src>` 文件，其顶层 import 被提升（说明符被
    重写以便从 HTML 文件解析），其主体被包装并注册；缺失文件与 import 绑定
    冲突作为诊断浮现。所有脚本按文档顺序运行（实际上是 deferred）。e2e
    覆盖。
  - **M9d** *(可选)* —— 检测传给 `win.loadHTML(...)` 的模板字面量中的内联
    HTML 并同样转换它们（脆弱；推迟）。
- **M10 — 打磨** ✅ —— 窗口上的
  `requestAnimationFrame`/`cancelAnimationFrame`、元素上的
  `offsetWidth`/`offsetHeight`/`contains`、文档。

### 落地顺序

1. M8（本分支）—— 纯引擎工作，无编译器影响。
2. M9a —— 通用钩子 + bundler 接线（可用假加载器做单元测试）。
3. M9b —— 最小端到端脚本路径（仅内联 `<script>`）。
4. M9c —— 外部 `<script src>`。
5. M10 —— 动画帧与辅助方法。

## 未决风险

- **句柄生命周期。** 通过代计数器 + 分离池解决；过期句柄变为无操作。`Node*`
  地址永不暴露给 TS。
- **无沙箱。** `<script>` 可以 `import fs`。记录为“不是浏览器”；真正的
  源/沙箱模型不在范围内。
- **动态 HTML 脚本。** 运行时创建的 HTML（`innerHTML`、网络获取）中的脚本
  不会运行。已记录。
- **模板字面量 HTML** 传给 `win.loadHTML(…)` 时在 M9b 中不会被转换（只有
  导入的资产会）。如有需要，M9d 会处理。
- **外部 `<script src>` 文件之间的 import 绑定冲突。** 会被检测并报告
  （给 import 起别名）；之后可以做到带引用重写的按脚本重命名。
- **资产。** `<script src>` 相对 HTML 资产解析；URL `src` 值被忽略。没有
  fetch/网络加载。

## 测试

- **M8**：`tests/e2e/gui-*.test.ts` 中的新 e2e 用例，覆盖句柄标识、遍历、特性、
  `classList`、内联 `style`、`createElement` + `appendChild` + `removeChild`、
  `getBoundingClientRect`、元素冒泡、`stopPropagation`、`el.click()` 以及
  **窗口级 `e.target` 兼容性**。
- **M9a**：一个假扩展加载器的单元测试，断言 bundler 重写 `*.foo` 并记录依赖。
- **M9b**：一个带内联 `<script>`、点击时递增计数器的 e2e 用例，通过处理器的
  `console.log` 断言。
- **M10**：`requestAnimationFrame`（每帧运行、重新排队、接收时间戳、遵守
  `cancelAnimationFrame`）与 `offsetWidth`/`offsetHeight`/`contains` 的 e2e
  用例。

## 需要更新的文档

- `gui.md` —— 非目标（“执行页面 `<script>`” → “执行*运行时*页面脚本；
  编译期脚本为 AOT 编译”）、锁定决策 #1、面向 TS 的 API、里程碑、进度日志。
- `implemented.md` / `unimplemented.md` —— 把新的 DOM/脚本能力相应迁移。
- `README.md` —— 提及 DOM API + AOT 脚本。
