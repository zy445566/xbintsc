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
  `border-radius` 与 `transform`；`transition: all` 覆盖它们。影响布局的
  属性（width、position 等）仍不做动画——那会每帧重新布局——但 `transform`
  是绘制期效果，因此为它做动画的代价只是每个被动画的盒子每帧一个矩阵
  （见[布局](./layout.md#变换m15)）。
- `transition` 简写与 `transition-property` / `-duration` / `-delay` /
  `-timing-function` 长写都会被解析；时间值接受 `s` 与 `ms`；缓动函数为
  `linear`、`ease`、`ease-in`、`ease-out` 与 `ease-in-out`（`ease` 是
  smoothstep 近似）。
- 在动画中途重定向会从当前插值启动一个新 transition，因此反转悬停会从它
  当前所在位置平滑动画。
- `computedStyle()` 报告显示（已插值）的值，因此 transition 在测试中可直接
  观察。

`transform` 列表按**逐项**插值（`document.cpp`、`xt_transform_interpolate`），
这正是让常见的一对 `translateY(-4px) rotate(-0.35deg)` 与 `none` 之间平滑
滑行而不是跳变的原因：缺失的一侧会用同类型的单位（identity）操作补齐。两个
列表形状或单位对不上时（`rotate(10deg)` 对 `rotate(10rad)`，或函数个数不同）
完全不做插值——transition 直接吸附到目标值，即 CSS 的离散回退，而不是一段
看起来错误的扫过。

## CSS transform（M15）

`transform` 与 `transform-origin` 作为*绘制期*效果得到支持
（`translate`/`translate3d`/`translateX`/`translateY`、
`scale`/`scale3d`/`scaleX`/`scaleY`、`rotate`/`rotateZ`、`matrix`；`3d` 拼写的
`z` 参数会被解析后丢弃，因为引擎是 2D 的；`deg`/`rad`/`grad`/`turn`；`%`、
`px`、`em`、`rem`、`vw`、`vh`）。布局不受影响：几何仍是流式布局放下的位置，只有绘制结果移动，
因此尺寸、滚动位置或兄弟节点的位置从不依赖某一动画帧。合成规则见
[布局](./layout.md#变换m15)，如何到达 GPU 见[绘制](./rendering.md)。

驱动方式有两种：

```ts
// 1. 逐帧——由帧时钟算出一个姿态。不会触发重排：字符串被解析成矩阵，
//    显示列表在同一帧里就取用它们。
const tick = (t: number): void => {
  const s = t / 1000;
  art.style.setProperty(
    "transform",
    "translateY(" + (Math.sin(s * 2.4) * 5).toFixed(2) + "px) " +
      "rotate(" + (Math.sin(s * 1.15) * 1.1).toFixed(2) + "deg)",
  );
  win.requestAnimationFrame(tick);
};
```

```css
/* 2. 由状态变化驱动——和任何其他属性一样参与 transition 的 transform。 */
#caption {
  transform-origin: 50% 100%;
  transition: transform 260ms ease-out;
}
#caption:hover { transform: translateY(-2px) scale(1.03); }
```

两种方式都由 `examples/gui/pelican-svg` 演示（逐帧摇摆 + 悬停时说明文字的
transition），并由 `tests/e2e/gui-transform.test.ts` 固定住。

动画仍需完成：`@keyframes` 动画与 `cubic-bezier(...)`。

