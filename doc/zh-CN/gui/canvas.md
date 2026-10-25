# Canvas：`<canvas>` 与 2D 上下文（M14）

> 属于 [GUI 扩展](../gui.md)；引擎总览、锁定决策与路线图见该文。

`<canvas>` 是一个**替换元素（replaced element）**，布局方式与 `<img>` 相同：其固有
尺寸来自 `width`/`height` 属性（默认 `300x150`，即 CSS 默认画布尺寸），显式的 CSS
`width`/`height` 优先；当只约束一个轴时保持宽高比。绘制阶段为每个 canvas 产出一个
`PaintImage`，其中携带的是 `canvas_id` 而非 `src`/`media_id`；渲染器通过与位图、视频帧
相同的图像路径（`renderer_image.cpp`）把位图上传为 RGBA 纹理，因此由 GPU 把它缩放到
CSS 盒中。

引擎侧的对象是 `xtgui::Canvas2D`（`runtime/ext_gui/canvas.{h,cpp}`）——一块 RGBA8 位图
加上 2D 绘制状态。光栅器位于 `canvas_raster.cpp`，描边几何（端点、连接、虚线）位于
`canvas_stroke.cpp`，渐变位于 `canvas_gradient.cpp`，绘制状态解析位于
`canvas_style.cpp`；DOM 绑定位于 `dom_api_canvas.cpp` / `dom_api_canvas_extra.cpp`；
按窗口的生命周期位于 `gui_canvas.cpp`。

## 获取上下文

```ts
const canvas: any = document.getElementById("c");
const ctx = canvas.getContext("2d"); // 其他上下文类型返回 null
```

`getContext("2d")` 返回一个上下文句柄，其方法与访问器位于共享原型上。该句柄会缓存在
元素上，因此每次都返回**同一个**对象，绘制状态（变换、样式、路径）按 canvas 而非按调用
保存。`ctx.canvas` 是所属元素的句柄。

## 绘制模型

光栅器刻意保持精简且为自研（不依赖 Skia/Cairo）。它按每条像素行 **4 条垂直子扫描线**
进行抗锯齿，并做一次**水平解析覆盖率**计算；路径采用**非零环绕规则**填充（因此当弧线
以相反方向环绕时能形成空洞）。轴对齐矩形（变换中 `b == 0 && c == 0`）走直接的覆盖率
快速路径。混合为非预乘 source-over，并按 `globalAlpha` 缩放。

填充或描边可以使用纯色，也可以使用 **`CanvasGradient`**。渐变的坐标属于用户空间，
因此光栅器会先用当前变换的逆矩阵把每个设备像素映射回用户空间再采样——形状被平移、
缩放或旋转时，渐变仍然贴在形状上。描边被展平为线段四边形，再按 `lineJoin`/`lineCap`
补上连接/端点形状；虚线模式先把折线切成“实线”片段，然后用同样的端点规则描边。

Canvas 像素**不参与垃圾回收**：canvas 由窗口拥有、以 DOM 节点为键（与
`<video>`/`<audio>` 解码表结构相同）。当节点被丢弃（`loadHTML`、窗口关闭/退出，或丢弃
子树的变更操作）时随之释放，并释放对应的 GPU 纹理。

## 支持的 API

状态与变换：`save` / `restore`（会恢复变换、线宽、透明度、字体/文本设置**以及**
填充/描边样式）、`translate`、`rotate`、`scale`、`transform(a,b,c,d,e,f)`、
`setTransform`、`resetTransform`、`getTransform`（返回带 `a`…`f` 与 `is2D` 的对象）。
变换按 canvas 的顺序组合：`translate(x, y); rotate(a)` 围绕平移后的原点旋转。

样式与设置：`fillStyle` / `strokeStyle`（层叠所用的颜色解析器能识别的 CSS 颜色，或
`CanvasGradient`）、`lineWidth`、`globalAlpha`、`lineCap`（`butt`/`round`/`square`）、
`lineJoin`（`miter`/`round`/`bevel`）、`miterLimit`、`lineDashOffset`、`font`
（CSS 字体简写，解析出字体族、字号与粗体/斜体——默认 `10px sans-serif`）、
`textAlign`（`start`/`left`、`center`、`right`/`end`）与 `textBaseline`
（`alphabetic`、`top`、`middle`、`bottom`、`hanging`、`ideographic`）。读取属性返回
上次赋予的值；赋予无法解析的颜色会保留原值。

渐变：`createLinearGradient(x0, y0, x1, y1)` 与
`createRadialGradient(x0, y0, r0, x1, y1, r1)` 返回带
`addColorStop(offset, color)` 的 `CanvasGradient`。色标是**活的**：在
`fillStyle = gradient` 之后追加的色标会影响后续填充，且 `fillStyle`/`strokeStyle`
的读取返回同一个渐变句柄。径向渐变按到**外圆**圆心（`x1, y1`）的距离插值，因此目前
会忽略 `x0`/`y0`——请使用同心圆（`x0 == x1`、`y0 == y1`）。

路径：`beginPath`、`closePath`、`moveTo`、`lineTo`、`rect`、`roundRect`、`arc`、
`ellipse`、`quadraticCurveTo`、`bezierCurveTo`，然后 `fill`（多环、非零环绕）或
`stroke`。贝塞尔与二次曲线段会被展平为线段；弧线与椭圆同样被展平。`roundRect` 只接受
单个圆角半径数字（数组形式尚未实现）。

矩形：`clearRect`、`fillRect`、`strokeRect`。

虚线：`setLineDash(segments)` / `getLineDash()` / `lineDashOffset` 实现标准规则
（奇数长度列表会复制一遍，负数/非有限值会被忽略，全零列表表示实线）。

文本：`measureText(text)`（返回 `{ width }`）、`fillText(text, x, y)` 与
`strokeText(text, x, y)`。文本经 HarfBuzz 排版、FreeType 光栅化（与 HTML 文本相同的
技术栈），按变换的近似缩放绘制。**仅支持平移与均匀缩放**——旋转或斜切变换仍会把每个
字形放到其变换后的基线点，但不会旋转字形位图。`strokeText` 目前用描边样式执行填充，
而非生成轮廓。

图像：`drawImage(source, …)` 支持 3 参数（`dx, dy`）、5 参数（`dx, dy, dw, dh`）与
9 参数（本引擎为**目标优先**：`dx, dy, dw, dh, sx, sy, sw, sh`）。完整的仿射变换
（旋转/斜切/缩放）会被应用：目标矩形变为平行四边形，每个设备像素反查到源矩形
（最近邻采样）。`source` 可以是 `<img>` 元素（经图像
模块解码）或另一个 `<canvas>` 元素（取其当前位图）。采样为最近邻，因此把小图大幅放大
会有块状感——适合图标与像素画，不适合照片管线。

测试钩子：`getImageData(x, y, w, h)` 把区域复制为 `{ width, height, data }`，其中
`data` 是按行主序的 RGBA8 字节数组。越界像素为透明。

## 尚未实现

图案（`createPattern`）、阴影、`clip()`、`globalCompositeOperation`、
`putImageData`、`createImageData`、`isPointInPath`、以 `DOMMatrix` 调用的
`setTransform`、可切换图像平滑的 `drawImage`、`toDataURL`/`toBlob`、把 `<canvas>` 用作
CSS `background-image`，以及离屏画布（`OffscreenCanvas`、
`transferControlToOffscreen`）。`roundRect` 只接受单个半径，径向渐变会忽略内圆偏移。

另见[媒体](./media.md)：`drawImage` 与纹理上传所共享的图像/视频/音频解码栈。
