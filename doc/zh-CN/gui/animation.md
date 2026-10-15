# 动画：帧与 transition

> 语言 / Language：[English](../../gui/animation.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

## 动画帧（M10）

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

## CSS transition（M6b）

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

