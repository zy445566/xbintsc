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
  每个文本片段会记住它覆盖的 run，以便绘制时对它整形。文本使用 HarfBuzz/FreeType
  技术栈测量（见[文本技术栈](./rendering.md)）。
- **Flexbox** —— `row`/`column`（以及 `-reverse` 变体），支持 `gap`、
  `flex-basis`/`flex-grow`/`flex-shrink`、`justify-content` 与 `align-items`
  （当交叉轴尺寸确定时包括 `stretch`）。`flex-wrap: wrap` 会把项断成多行，
  `align-content`（`stretch`/`flex-start`/`flex-end`/`center`/`space-between`/
  `space-around`）在交叉轴上分配这些行，`wrap-reverse` 则翻转交叉轴。
  flex 项的外边距参与其外框尺寸，因此
  负的 `margin-top` 会把该项上移，而不是被忽略。项之间的纯空白文本
  （源码缩进）不会成为 flex 项。
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

**Document**（`runtime/ext_gui/document.{h,cpp}`）：持有 DOM 树，把 `<style>`
文本汇总为一份样式表，为某个视口计算样式与布局，并提供
`querySelector`/`querySelectorAll`/`styleOf`/`boxOf`。

