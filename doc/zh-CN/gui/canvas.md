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
`canvas_stroke.cpp`，渐变位于 `canvas_gradient.cpp`，合成模式位于
`canvas_composite.cpp`，裁剪与命中测试位于 `canvas_clip.cpp`，阴影位于
`canvas_shadow.cpp`，绘制状态解析位于 `canvas_style.cpp`；DOM 绑定位于
`dom_api_canvas*.cpp` / `dom_api_offscreen.cpp`；按窗口的生命周期位于
`gui_canvas.cpp`。

## 获取上下文

```ts
const canvas: any = document.getElementById("c");
const ctx = canvas.getContext("2d"); // 或 "webgl" 取得 GPU 上下文
```

一个 canvas 只有一种上下文。`getContext("webgl")` 或 `getContext("webgl2")`
返回 WebGL 上下文（见 [WebGL](./webgl.md)）；一旦 canvas 有了 2D 上下文，
再申请任一 WebGL 类型都会返回 `null`，反之亦然。从未申请过上下文的 canvas
保持透明。

`getContext("2d")` 返回一个上下文句柄，其方法与访问器位于共享原型上。该句柄会缓存在
元素上，因此每次都返回**同一个**对象，绘制状态（变换、样式、路径）按 canvas 而非按调用
保存。`ctx.canvas` 是所属元素的句柄。

`OffscreenCanvas` 提供同样的 2D 接口，但没有 DOM 元素：
`new OffscreenCanvas(width, height)` 分配一块由窗口拥有的位图，`getContext("2d")`
返回的上下文其 `canvas` 指向离屏对象；`canvas.transferControlToOffscreen()` 会把一个
活动元素的位图交给离屏对象（此后元素的 `getContext(…)` 返回 `null`）。
`drawImage`、`createPattern`、`createImageBitmap` 与 `texImage2D` 都接受
`OffscreenCanvas` 作为源。`transferToImageBitmap()` 返回 `ImageBitmap`，
`convertToBlob()` 解析为与 `toBlob` 相同的 PNG blob 形状（本运行时没有 worker，
离屏画布也不提供 WebGL 上下文）。

`getContext("2d", attributes)` 在首次调用时会尊重 `alpha`：传入 `{ alpha: false }`
时位图以不透明黑色初始化（清除也回到不透明黑）。其余属性由
`getContextAttributes()` 按规范默认值报告（`antialias: true`、`depth: true`、
`stencil: false`、`premultipliedAlpha: true`、`preserveDrawingBuffer: false`、
`colorSpace: "srgb"` 等），特性探测因此能得到诚实的答案。

## 绘制模型

光栅器刻意保持精简且为自研（不依赖 Skia/Cairo）。它按每条像素行 **4 条垂直子扫描线**
进行抗锯齿，并做一次**水平解析覆盖率**计算；路径默认采用**非零环绕规则**填充（因此当弧线
以相反方向环绕时能形成空洞），`fill("evenodd")` 则改用奇偶规则。轴对齐矩形（变换中
`b == 0 && c == 0`）走直接的覆盖率快速路径。

混合默认是非预乘 source-over。`globalCompositeOperation` 支持完整的 canvas 集合：
Porter-Duff 算子（`source-over`、`source-in`、`source-out`、`source-atop`、
`destination-over`、`destination-in`、`destination-out`、`destination-atop`、
`lighter`、`copy`、`xor`）以及可分离/不可分离混合模式（`multiply`、`screen`、
`overlay`、`darken`、`lighten`、`color-dodge`、`color-burn`、`hard-light`、
`soft-light`、`difference`、`exclusion`、`hue`、`saturation`、`color`、
`luminosity`）。抗锯齿与裁剪区域产生的部分覆盖率会在“未改动的目标”与“完整合成结果”
之间插值，因此每个算子在部分覆盖的像素上都正确。

`clip()` 用当前路径（或 `Path2D` 加可选的 `"nonzero"`/`"evenodd"` 规则）与当前裁剪区
求交。区域以设备空间覆盖率掩码保存，因此它固定在调用 `clip()` 时的变换下；
`save`/`restore` 会一并恢复，位图尺寸变化会清空它。所有填充、描边、文本、图像绘制与
`clearRect` 都遵守该掩码。

阴影（`shadowColor`、`shadowBlur`、`shadowOffsetX`、`shadowOffsetY`）由绘制操作自身的
alpha 生成：先把操作渲染到临时图层，对其 alpha 做偏移与盒式模糊（三次可分离模糊），用
`shadowColor` 着色后合成，最后把未处理的图层画在其上。偏移与模糊使用画布（设备）单位，
不受变换影响，符合 API 规定。`clearRect` 与 `putImageData` 从不产生阴影。

`isPointInPath(x, y[, fillRule])`、`isPointInPath(path, x, y[, fillRule])`、
`isPointInStroke(x, y)` 与 `isPointInStroke(path, x, y)` 在当前变换下对当前路径（或
传入的 `Path2D`）做命中测试；描边命中测试使用到各展平线段的设备空间距离与当前线宽。

填充或描边可以使用纯色，也可以使用 **`CanvasGradient`**。渐变的坐标属于用户空间，
因此光栅器会先用当前变换的逆矩阵把每个设备像素映射回用户空间再采样——形状被平移、
缩放或旋转时，渐变仍然贴在形状上。描边被展平为线段四边形，再按 `lineJoin`/`lineCap`
补上连接/端点形状；虚线模式先把折线切成“实线”片段，然后用同样的端点规则描边。

Canvas 像素**不参与垃圾回收**：canvas 由窗口拥有、以 DOM 节点为键（与
`<video>`/`<audio>` 解码表结构相同）。当节点被丢弃（`loadHTML`、窗口关闭/退出，或丢弃
子树的变更操作）时随之释放，并释放对应的 GPU 纹理。

## 性能

canvas 由脚本重绘而不是引擎重绘，因此开销按绘制调用计。多元素场景下的热路径刻意
保持少分支：

- 纯色不透明的 `fillRect` 直接按字节写入位图（小数边缘仍走抗锯齿路径），覆盖整张
  位图的 `clearRect` 退化为一次 `memset`；
- 路径覆盖率使用逐行**活动边表**光栅化，并且只物化每一行**实际覆盖的区间**——一条
  细对角线描边只花它自己的像素，而不是整个包围盒；
- 一次 `stroke()` 把所有线段四边形、连接与端点合并成一次非零环绕填充，而不是每段
  各做一次覆盖率计算（先统一每个多边形的绕向，重叠处才能覆盖而不是抵消）；
- `fillText` 与布局共享整形缓存（`text_cache.cpp`），并使用有界两代字形位图缓存
  （`text_glyph_cache.cpp`）；
- 整数偏移、无缩放的 `drawImage` 逐像素只取一个纹素，不再跑双线性采样器。

位图通过**脏矩形**到达屏幕：每次绘制把它的设备空间包围盒并入待上传区域，渲染器只
拷贝该区域到 canvas 纹理。一帧内所有 canvas 与视频上传共用帧的唯一上传命令缓冲，
因此 K 张动画 canvas 只需一次提交而不是 K 次。`getImageData` 始终读取 CPU 位图，
不受影响。`tests/e2e/gui-draw-perf.test.ts` 测量这些多元素路径并固定计数器行为
（每帧一次上传、只传脏字节）。

## 支持的 API

状态与变换：`save` / `restore`（会恢复变换、线宽、透明度、字体/文本设置**以及**
填充/描边样式）、`translate`、`rotate`、`scale`、`transform(a,b,c,d,e,f)` /
`transform(matrix)`、`setTransform(a,b,c,d,e,f)` / `setTransform(matrix)`、
`resetTransform`、`getTransform`（返回带 `a`…`f` 与 `is2D` 的对象）。矩阵可以是
`{ a, b, c, d, e, f }` 对象或六元素数组。变换按 canvas 的顺序组合：
`translate(x, y); rotate(a)` 围绕平移后的原点旋转。

样式与设置：`fillStyle` / `strokeStyle`（层叠所用的颜色解析器能识别的 CSS 颜色，或
`CanvasGradient`）、`lineWidth`、`globalAlpha`、`globalCompositeOperation`、
`shadowColor` / `shadowBlur` / `shadowOffsetX` / `shadowOffsetY`、
`imageSmoothingEnabled` / `imageSmoothingQuality`、`lineCap`（`butt`/`round`/`square`）、
`lineJoin`（`miter`/`round`/`bevel`）、`miterLimit`、`lineDashOffset`、`font`
（CSS 字体简写，解析出字体族、字号与粗体/斜体——默认 `10px sans-serif`）、
`textAlign`（`start`/`left`、`center`、`right`/`end`）与 `textBaseline`
（`alphabetic`、`top`、`middle`、`bottom`、`hanging`、`ideographic`）。读取属性返回
上次赋予的值；赋予无法解析的颜色会保留原值。`imageSmoothingEnabled` 默认为 `true`
（双线性采样）；`imageSmoothingQuality` 报告 `low`、`medium` 或 `high`，本后端都使用
同一种双线性过滤。

渐变：`createLinearGradient(x0, y0, x1, y1)` 与
`createRadialGradient(x0, y0, r0, x1, y1, r1)` 返回带
`addColorStop(offset, color)` 的 `CanvasGradient`。色标是**活的**：在
`fillStyle = gradient` 之后追加的色标会影响后续填充，且 `fillStyle`/`strokeStyle`
的读取返回同一个渐变句柄。径向渐变是通用的双圆锥：点上的颜色由插值圆
（`圆心 = c0 + t·(c1 − c0)`、`半径 = r0 + t·(r1 − r0)`）解出，因此内圆有偏移、两圆
半径不同时都与浏览器一致。内圆内部的点采样色标 0。

路径：`beginPath`、`closePath`、`moveTo`、`lineTo`、`rect`、`roundRect`、`arc`、
`ellipse`、`quadraticCurveTo`、`bezierCurveTo`，然后
`fill(fillRule?)` / `fill(path, fillRule?)`（多环；`"nonzero"` 或 `"evenodd"`）、
`stroke(path?)`、`clip(path?, fillRule?)` 以及 `isPointInPath`/`isPointInStroke`
命中测试。贝塞尔与二次曲线段会被展平为线段；弧线与椭圆同样被展平。
`roundRect` 接受单个半径数字，或 CSS 半径列表（`[all]`、`[tl, br]`、
`[tl, tr, br]`、`[tl, tr, br, bl]`，元素为数字或 `{ x, y }` 对；圆角按圆弧绘制）。

`Path2D` 支持同一组路径命令（`moveTo`、`lineTo`、`closePath`、`rect`、
`roundRect`、`arc`、`ellipse`、`quadraticCurveTo`、`bezierCurveTo`、`addPath`），
并可传给 `fill`/`stroke`/`clip` 以及 `isPointIn*` 命中测试，因此一次构建的路径可以在
不同变换下重复使用。

矩形：`clearRect`、`fillRect`、`strokeRect`。

虚线：`setLineDash(segments)` / `getLineDash()` / `lineDashOffset` 实现标准规则
（奇数长度列表会复制一遍，负数/非有限值会被忽略，全零列表表示实线）。

文本：`measureText(text)`（返回 `{ width }`）、`fillText(text, x, y, maxWidth?)` 与
`strokeText(text, x, y, maxWidth?)`。文本经 stb_truetype 整形与光栅化（与 HTML
文本相同的技术栈），按变换的近似缩放绘制。正的 `maxWidth` 会按比例压缩步进与字形位图，
使文本宽度不超过该值（与 canvas API 一致），而不是忽略该参数。**仅支持平移与均匀
缩放**——旋转或斜切变换仍会把每个字形放到其变换后的基线点，但不会旋转字形位图。
`strokeText` 目前用描边样式执行填充，而非生成轮廓。

图像：`drawImage(source, …)` 支持 3 参数（`dx, dy`）、5 参数（`dx, dy, dw, dh`）与
9 参数（本引擎为**目标优先**：`dx, dy, dw, dh, sx, sy, sw, sh`）。完整的仿射变换
（旋转/斜切/缩放）会被应用：目标矩形变为平行四边形，每个设备像素反查到源矩形。
`source` 可以是 `<img>` 元素（经图像模块解码）、另一个 `<canvas>`、
`OffscreenCanvas`、`ImageBitmap`，或——本引擎的扩展——直接传字符串路径/`data:` URL，
由图像模块解码，因此 SVG 或位图的 data URL 无需 `<img>` 就能绘制。矢量源按目标尺寸
（`dw`/`dh`）光栅化，放大时 SVG 依然清晰。`imageSmoothingEnabled` 为 `true`（默认）
时使用双线性采样，为 `false` 时使用最近邻。

测试钩子：`getImageData(x, y, w, h)` 把区域复制为 `{ width, height, data }`，其中
`data` 是按行主序的 RGBA8 字节数组。越界像素为透明。

图案：`createPattern(source, repetition?)` 接受 `<img>`、`<canvas>`、
`OffscreenCanvas` 或 `ImageBitmap`，在用户空间中平铺（`repeat`、`repeat-x`、
`repeat-y`、`no-repeat`），因此平铺会跟随当前变换。把结果赋给
`fillStyle`/`strokeStyle`；重新赋颜色会替换掉图案。

图像数据与导出：`createImageData(width, height)` 分配一个空的
`{ width, height, data }`，`putImageData(imageData, dx, dy[, dirtyX, dirtyY,
dirtyWidth, dirtyHeight])` 直接把字节写入位图（不受变换、混合、裁剪或
`globalAlpha` 影响）。`canvas.toDataURL(type?)` 导出 PNG data URL，
`canvas.toBlob(callback, type?)` 从微任务中交付 Blob 形状的 `{ type, size }`
（字节在 `__xt_gui_blob_data` 上；本运行时没有 `URL.createObjectURL`）。
`OffscreenCanvas.convertToBlob()` 解析为同一形状，`transferToImageBitmap()` 对位图
做快照。

CSS 背景：`background-image: url(...)` 接受文件路径或 `data:` URL（位图载荷或 SVG
文档，base64 或百分号编码均可），因此可以先把 canvas 导出再作为 CSS 背景使用
（`element.style.backgroundImage = "url(" + canvas.toDataURL() + ")"`）。图像层会拉伸
到元素的边框盒；`background-size`、`background-position` 与平铺尚未建模。

## 尚未实现

本后端的平滑质量只有一种过滤（双线性）。离屏画布只支持 2D 上下文（WebGL 上下文需要
DOM 元素来维护纹理表）。旋转的字形位图仍不会随变换旋转，`strokeText` 仍用描边样式
填充而非生成轮廓。

另见[媒体](./media.md)：`drawImage` 与纹理上传所共享的图像/视频/音频解码栈。
