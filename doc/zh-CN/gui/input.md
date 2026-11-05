# 输入与事件（M5）

> 语言 / Language：[English](../../gui/input.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。
> 公开的事件 API 见 [api.md](./api.md)。

`LayoutTree::hitTest` 返回包含某点的最深盒（先探测靠后的兄弟，使最上层元素
胜出），`xt_dom_describe` 把元素转换为事件负载携带的 `div#id.class` 描述符。
被变换的盒子先把点经其逆矩阵映射，因此旋转、缩放或平移过的元素恰好在它**被画
出来**的位置被命中——`:hover` 跟着画面走，而不是跟着流式布局留下的盒子走。

- SDL 指针/滚轮/键盘事件会被路由到所属窗口，进行命中测试，并投递给用 `on`
  注册的处理器。`mousedown` 还会移动焦点，并在文本框中放置光标。
- 键盘事件同时走 DOM 路径：目标为聚焦元素（没有焦点时为 `<body>`），事件沿祖先
  冒泡到 `document`，最后到达 `window` 监听器。因此按浏览器写的
  `document.addEventListener("keydown", …)` 可以直接工作。负载使用浏览器
  `KeyboardEvent` 的写法：`code` 是物理键（`"KeyW"`、`"ArrowUp"`、`"Space"`），
  `key` 是产生的值（`"w"`、`"W"`、`" "`），而不是 SDL 的扫描码/键名。
- `:hover` 匹配悬停元素**及其祖先**（因此悬停子元素会点亮其父元素）；
  `:focus` 匹配聚焦元素。当任一改变时，`XtDocument::setHover`/`setFocus`
  重新计算样式与布局，并把窗口几何标记为脏，因此改动会在下一帧绘制。
- `win.hitTest(x, y)` 与 `win.sendEvent(type, options)` 暴露命中测试与合成
  输入，使整条路径可无头测试（e2e 套件在没有真实鼠标的情况下驱动点击、滚轮
  与按键）。

## 表单控件（M23）

`<input>`、`<textarea>` 与 `<select>` 是引擎原生的叶子盒：值、选区与勾选状态
保存在 `Node::form`（`FormState`）中，由引擎绘制，不涉及浏览器/DOM 层。

```ts
const user = document.getElementById("user"); // <input id="user" value="abc">
user.focus();
user.value;                 // "abc"
user.selectionStart;        // 3
user.setSelectionRange(1, 3);
user.placeholder = "name";
user.disabled = true;
```

- **`<input>` 类型**：`text`、`search`、`tel`、`url`、`email`、`password`、
  `number`、`date`、`month`、`week`、`time`、`datetime-local`、`range`、`color`、
  `checkbox`、`radio`、`file`、`submit`、`reset`、`button`、`image` 与 `hidden`。
  文本类可编辑；checkbox/radio 绘制方框并在点击时切换（radio 会取消同 `name`
  组的其它项）；range 绘制轨道与滑块并支持拖动；color 绘制色块；
  submit/reset/button 绘制标签；`hidden` 不产生盒。
- **`<textarea>`** 支持多行（`\n` 换行），**`<select>`** 绘制当前 `<option>`
  的标签，点击切换选项。
- **`value`** 是活动值，与 `value` 属性（默认值）分离。`<textarea>` 的默认值
  来自其文本内容，`<select>` 来自被选中/第一个 `<option>`。给 `value` 赋值会
  把光标移到末尾，与浏览器一致。
- **`selectionStart` / `selectionEnd` / `selectionDirection`**、
  `setSelectionRange(start, end, direction)` 与 `select()` 使用 UTF-16 码元
  偏移（DOM 约定），底层存储为 UTF-8。
- **`checked`**、`type`、`name`、`disabled`、`readOnly`、`required`、
  `placeholder`、`min`、`max`、`step`、`maxLength`、`tabIndex`、`form`、
  `options` 与 `selectedIndex` 均为活动 DOM 属性。
- **`document.activeElement`** 是聚焦元素；没有焦点时为 `<body>`。`tabindex`
  决定 Tab 顺序（`tabindex="1"` 排在隐式 `0` 之前；`-1` 可点击聚焦但被 Tab
  跳过）；即使没有文本框聚焦，Tab 与 Shift+Tab 也能移动焦点。禁用或隐藏的控件
  不能聚焦、会被 Tab 跳过，并以灰色绘制。
- **文本输入**来自 `SDL_EVENT_TEXT_INPUT`，它携带键盘布局或 IME 实际产生的
  文本。`keydown` 处理编辑命令（Backspace/Delete/方向键/Home/End/Ctrl+A/C/X/V，
  Shift 扩展选区），编辑后派发 `keypress` 与 `input`。
- **IME** 使用 `SDL_EVENT_TEXT_EDITING`：预编辑文本显示在光标处，并触发
  `compositionstart` / `compositionupdate` / `compositionend`；提交内容在
  `compositionend` 时插入值中。
- **剪贴板**通过 SDL 使用系统剪贴板：Ctrl+C/X/V。
- **事件**自动派发：文本编辑前触发可取消的 `beforeinput`，每次值变化后触发
  `input`（输入、删除、checkbox/radio 切换、range 拖动、select 切换），值提交时
  触发 `change`（有改动的文本框失焦、单行输入框回车、checkbox/radio 点击、
  range 释放或 select 切换）。`input`/`change` 负载携带 `value`；`change` 还
  携带 `checked`。
- **表单**：提交按钮（或单行输入框回车）在所属 `<form>` 上触发可取消的
  `submit`，被点击时携带 `submitter`；引擎不会导航，因此是否阻止事件在视觉上
  没有区别。重置按钮或 `form.reset()` 触发 `reset`，并把每个控件恢复到默认
  值/勾选状态。`form.submit()` 与 `form.requestSubmit(submitter?)` 派发同一个
  事件。

`win.sendEvent` 也接受 `{ type: "textInput", text }`（等同于 SDL 投递该文本）、
`{ type: "compositionstart" | "compositionupdate" | "compositionend", data }` 与
可打印的 `keydown`。真实的物理 `keydown` 不会插入其 US 布局的 `key`：对应的
文本输入事件才会插入，因此 IME 或非 US 布局不会重复字符。

完整输入仍需完成：跨多行的拖动选择与撤销/重做。
