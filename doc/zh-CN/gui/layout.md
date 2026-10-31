# 布局

> 语言 / Language：[English](../../gui/layout.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

## 布局引擎（M3）

`runtime/ext_gui/layout.{h,cpp}` 把带样式的 DOM 转换为具有绝对（相对视口）
几何信息的 `LayoutBox` 树：

- **块级流** —— 块级子元素垂直堆叠；`display: none`
  不生成盒；`width: auto` 填满包含块，`height: auto` 包裹内容。盒模型
  （margin/padding/border）会被解析，包括相对包含块宽度的百分比。相邻兄弟的
  垂直外边距会折叠为 `max(正值) + min(负值)`；父元素的下外边距不会与其最后一个
  子元素折叠（见下方限制）。
- **行内流** —— 连续的行内级子元素构成一个匿名行内格式化上下文，采用贪心、
  基于单词的断行、`text-align` 与 `line-height`。行内元素获得其后代几何信息
  的并集；`display: inline-block` 以 shrink-to-fit 宽度原子化布局。每一行会收缩到
  与之重叠的浮动（`float: left|right`）所留出的空白带，因此后续行会绕开它们。
  每个文本片段会记住它覆盖的 run，以便绘制时对它整形。文本使用 stb_truetype
  技术栈测量（见[文本技术栈](./rendering.md)）。
- **Flexbox** —— `row`/`column`（以及 `-reverse` 变体），支持 `gap`、
  `flex-basis`/`flex-grow`/`flex-shrink`、`justify-content` 与 `align-items`
  （当交叉轴尺寸确定时包括 `stretch`）。`flex-wrap: wrap` 会把项断成多行，
  `align-content`（`stretch`/`flex-start`/`flex-end`/`center`/`space-between`/
  `space-around`）在交叉轴上分配这些行，`wrap-reverse` 则翻转交叉轴。
  flex 项的外边距参与其外框尺寸，因此
  负的 `margin-top` 会把该项上移，而不是被忽略。项之间的纯空白文本
  （源码缩进）不会成为 flex 项。
- **Grid** —— `display: grid`，支持 `grid-template-columns` /
  `grid-template-rows`（固定长度、`auto`、`<n>fr`、`minmax(...)`、
  `repeat(...)`）、`gap`/`row-gap`/`column-gap`、按行自动放置、简单的
  `grid-column`/`grid-row` 线定位，以及 `align/justify-items` 与
  `align/justify-self`（含 `place-items`/`place-self`）。隐式行/列由项目撑开；
  单单元格网格配合 `place-items: center` 可把卡片居中到整个覆盖层。项目测量是
  两遍布局（先固有尺寸、再最终放置），因此两遍之间会清空文本片段。
- **行内级容器、自动外边距与宽高比** —— `display: inline-flex`/`inline-grid`
  仍用 flex/grid 算法排列子元素，但作为单个原子行内盒参与父级行流
  （`vertical-align` 的长度值会平移它）。块级盒的 `margin-left: auto` /
  `margin-right: auto` 会吸收行内剩余空间（双 auto 则居中）；flex 项主轴的 auto
  外边距在 `justify-content` 之前吸收该行剩余空间，因此可把页脚或按钮提示推到
  末端。`aspect-ratio` 在 `height: auto` 时由已用宽度推导高度（反向的由高推宽
  未实现）。`border-radius` 支持百分比，并按 border box 较小边解析。
- **竖排书写模式** —— `writing-mode: vertical-rl|vertical-lr` 按每列一个字符
  步进排字：CJK/假名/谚文字符保持直立，其他文字在绘制时旋转 90°。高度确定时
  另起一列；flex 容器的 row/column 轴随书写模式互换。这是面向文本的实现：
  竖排容器中的嵌套块与横竖混排内容仍回退为横排。
- **CSS 值函数与自定义属性** —— `calc()` 算术（含嵌套 `calc()` 以及其它函数
  内部的 `calc()`）、`clamp(min, value, max)`、`min(...)` 与 `max(...)` 在计算
  样式中保留表达式，布局时结合元素字号与视口（`vw`/`vh`）求值；桌面窗口中
  `env(safe-area-inset-*)` 解析为 0。`var(--name[, fallback])` 在
  层叠期间从继承的自定义属性替换。`inset` 是四值 `top/right/bottom/left`
  简写；`box-sizing: border-box` 会从显式宽高里扣除 padding 与边框；
  `visibility: hidden` 跳过绘制与命中测试；`pointer-events: none` 使盒子不接收输入
  （后代可用 `auto` 重新启用）。
- **定位** —— `position: relative` 会按 `top`/`right`/`bottom`/`left` 偏移该盒子
  及其子树，但不改变它在流中占用的空间；`position: absolute`（以及针对视口解析
  的 `fixed`）使其脱离文档流，并相对最近的已定位祖先的 padding box 定位（没有
  这样的祖先时则相对视口）。`auto` 的 inset 在该轴上保留静态位置；宽度 `auto`
  时使用 shrink-to-fit，除非 `left` 与 `right` 同时给出（此时填满两者之间的空间）；
  百分比相对包含块解析。层叠通过绘制前按 `z-index` 对每个父元素的子元素排序
  （负值 → `auto` → 正值）来近似实现；这只是一个层级，并非完整的层叠上下文，
  因此子元素无法脱离父元素的层级。
  注意该 shrink-to-fit 规则的后果：一个 `width: auto` 且没有显式尺寸的绝对定位
  盒子，其内部行内内容得到的**可用宽度为 0**（它的首选宽度只来自文本与
  非绝对定位的子元素），因此其中的行内 run 会各自换到独立的一行。请为此类盒子
  给出显式 `width`，或用 `left`/`width` 摆放其子元素（`examples/gui/pelican-bike`
  的精灵 run 就是这样做的），而不要依赖其内部的行流。

- **浮动与清除** —— `float: left|right` 使盒子脱离文档流，将其宽度设为
  shrink-to-fit，放在当前行旁，并让后续行内行绕开它；`clear: left|right|both`
  会把后续块推到相关浮动的下方，而只包含浮动的块会增高以包裹它们。
- **`overflow` 裁剪** —— `overflow: hidden`（或 `scroll`/`auto`）的盒子会将其
  子树裁剪到自身的 padding box，并与任一祖先裁剪区域取交集；渲染器用 GPU
  scissor 矩形实现该结果。目前还没有滚动，因此 `scroll`/`auto` 与 `hidden`
  一样只做裁剪。

尚未实现：父/子与空块的外边距折叠、由非空白文本产生的匿名 flex 项、完整的
`z-index` 层叠上下文，以及 `float` 对块级子元素的影响（只有行内内容会绕排，
块必须 `clear`）。

## 变换（M15）

`transform` 与 `transform-origin` 是**绘制期**效果：上面的盒模型完全不变
（不重排、不重算几何），`xt_layout_run` 之后的一次额外遍历会给每个
`LayoutBox` 一个 `Matrix2D world`（整条祖先链的变换）及其 `inverse`：

- **只有元素盒参与。** 文本盒与其父元素共享同一个 `style` 指针，若两者都参与
  就会把元素的列表应用两次；该遍历只合成元素自身的列表与祖先的。
- **`transform-origin` 在这里解析**，而不是在层叠阶段：计算样式保留两个
  （可能是 `%` 的）`Length`（默认 `50% 50%`），布局按*边框盒*解析它们，应用
  `T(origin) · M · T(-origin)`。这就是
  `computedStyle(el, "transform-origin")` 仍报告 `50% 92%` 的原因。关键字与
  CSS 一样带方向：单独写 `top` 等于 `center top`，两个关键字可以任意顺序
  （`bottom right` 等于 `right bottom`）。
- **长度按盒子解析**：`translate()` 里的 `%` 是边框盒宽/高的比例，`em`/`rem`
  按根字号（16px），`vw`/`vh` 按视口。
- **合成顺序**遵循 CSS：列表从右往左应用，所以
  `rotate(90deg) translate(10px)` 把盒子往屏幕**下方**移动，而不是往右。

下游消费者都跟着矩阵走：

| 消费者 | 行为 |
| --- | --- |
| 绘制 | 每个 op 携带矩阵（见[绘制](./rendering.md#绘制与显示列表m4am4b)）；被裁剪的子树按它的内边距盒经矩阵变换后的包围盒裁剪 |
| 命中测试 | `hitTestBox` 先把点经每个盒的逆矩阵映射，再做包含判断，所以旋转或缩放的元素在它**被画出来**的位置可点击 |
| `getBoundingClientRect` | **变换后**边框盒的轴对齐包围盒（`xt_layout_transformed_bounds`） |
| 布局几何 | 不变——`layoutTree()` 与 `offsetWidth`/`offsetHeight` 仍报告未变换的盒子 |

不支持的变换函数（`skew`、`perspective` 等）会让整条声明失效，因此一个盒子
要么被正确变换、要么完全不变换，而不是只应用了一部分。

**Document**（`runtime/ext_gui/document.{h,cpp}`）：持有 DOM 树，把 `<style>`
文本汇总为一份样式表，为某个视口计算样式与布局，并提供
`querySelector`/`querySelectorAll`/`styleOf`/`boxOf`。

