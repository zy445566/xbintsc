# 输入与事件（M5）

> 语言 / Language：[English](../../gui/input.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。
> 公开的事件 API 见 [api.md](./api.md)。

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

