# DOM、HTML、CSS 与 AOT 脚本

> 语言 / Language：[English](../../gui/dom.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。
> 脚本接口另有文档：[gui-scripts.md](../gui-scripts.md)。

## HTML/CSS 子集（M3a）

**HTML 解析器**（`runtime/ext_gui/dom.{h,cpp}`）：标签/属性/文本，注释与
doctype 跳过，实体解码（命名 + 十进制/十六进制），空元素，原始文本元素
（`<style>`/`<script>`），以及常见的隐式闭合规则（`li`、`dt`/`dd`、`option`、
`p`、标题、表格单元格/行）。

**CSS 解析器**（`runtime/ext_gui/css.{h,cpp}`）：会剥离注释；`@media` 块递归解析
（嵌套查询用 `and` 合并，逗号列表会分配到内层查询），其他 at-rule 跳过。规则支持
多选择器与声明；选择器列表可以跨任意多行书写（换行就是普通空白，换行后的
组合器仍按组合器处理），引号内的属性值即使包含逗号、方括号或圆括号也不会
拆开列表。选择器：类型、`.class`、`#id`、属性
（`=`、`~=`、`|=`、`^=`、`$=`、`*=`）、四种组合器（后代、子、相邻兄弟与
通用兄弟）、伪类 `:first-child`、`:last-child`、`:only-child`、`:empty`、
`:root`、`:not(...)`、`:nth-child(an+b)`、`:disabled`、`:checked`、有状态的
`:hover` 与 `:focus`（见[输入](./input.md)），以及伪元素
`::before`/`::after`（也接受旧式 `:before`/`:after`）。值：长度
（`px`、`%`、`em`、`rem`、`vw`、`vh`、`pt`、`pc`、`in`、`cm`、`mm`、`q`）、
颜色（十六进制、`rgb()`/`rgba()`、一个命名子集）、数字、关键字与简写
（`margin`/`padding`/`border`/`flex`）。`transform`
（`translate`/`translate3d`/`translateX`/`translateY`、
`scale`/`scale3d`/`scaleX`/`scaleY`、`rotate`/`rotateZ`、`matrix`；`3d`
拼写的 `z` 参数会被丢弃）与 `transform-origin`
会被解析进计算样式：前者是操作列表，后者是两个（未解析的）原点长度；不支持的
函数会让整条声明失效（见[布局](./layout.md#变换m15)）。M19 增加了
`background-image` 层列表（线性/径向）、`clip-path`
（`inset`/`circle`/`ellipse`/`polygon`）、`filter`/`backdrop-filter` 函数列表、
`text-shadow` 与 `content`。

**层叠与计算样式**（`runtime/ext_gui/style.{h,cpp}`）：一份小的内置 UA
样式表，作者规则按 `(!important, 特异性, 源码顺序)` 排序，然后是内联
`style=""`（特异性最高，但非 `!important` 的内联会输给 `!important`），
再加上文本属性的 CSS 继承。每次重算样式都会按当前视口过滤 `@media` 规则，
因此调整窗口大小会切换它们。相对长度保持未解析状态直到布局阶段，`font-size`
除外（相对*父级*字号解析）以及 `line-height`。

`::before`/`::after` 的样式在原始元素之后计算：选择器最后一个复合选择器必须写
伪元素，其余部分对原始元素匹配（因此 `.card.info::before` 可用）。伪样式继承
原始元素的计算样式，`content` 会被解析（字符串、转义、`attr()`、引号），
生成的合成节点存放在每次重算样式时重建的 arena 中。布局把它的盒子插在真实子元素
之前/之后；遍历、查询、`innerHTML` 与 `elementIndex()` 永远看不到它，命中测试
则指向原始元素。

## 交互式 DOM（M8）

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
捕获与冒泡阶段、`stopPropagation`、`once`、`passive` 与 `signal`（`AbortController`
的信号会在 abort 时移除监听器），并向上冒泡到 `document`/`window`。
`dispatchEvent` 派发同一个事件对象，并填充 `target`/`currentTarget`/`eventPhase`/
`composedPath()`。旧式 `win.on(type, fn)` 负载保留其**字符串** `e.target`
（`div#id.class`）；元素 `Event.target` 是一个句柄，其描述符与同一字符串匹配。

### 节点生命周期

一次变更会决定它移除的节点是否仍可到达，引擎只释放那些确实不可到达的节点：

| 变更 | 被移除的节点 |
| --- | --- |
| `innerHTML = …`、`inner = …`、`textContent = …`、`replaceChild` | **被释放**——脚本再也无法引用它们 |
| `removeChild`、`remove` | **保留**在文档的分离节点池中——DOM 语义要求被移除的节点继续存活且可用，以便重新插入 |

被释放的节点会同时释放其 C++ 子树与句柄，因此 `win.stats()` 的计数器
（`detachedNodes`、`handles`、`handleSlots`）会回到先前的数值。这正是常见游戏
写法——每帧用 `innerHTML` 重写精灵——能保持常量内存的原因；在此之前，每个被
替换的子树都会永久留在池中，每帧写入十个节点的循环会以每秒数 MB 的速度泄漏。

引擎唯一无法察觉的情况是：脚本创建了一个节点，随后从未插入就丢弃它。
`document.createElement("div")` 会把节点放进池中，**并且**让它的句柄保持可达，
因为标记-清扫收集器没有弱引用，引擎无法知道脚本已经丢弃了该句柄。因此，
只创建、不插入的循环会让池持续增长。请复用元素，或用 `innerHTML` 构建内容
（它会释放被替换的节点），而不要每帧新建元素。

## AOT 脚本（M9）

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

