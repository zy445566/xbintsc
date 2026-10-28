# 鹈鹕骑自行车（SVG）— Pelican Bike as vector art

一只骑自行车的鹈鹕，画成 **SVG 矢量图**，由 `gui` 扩展在原生窗口里显示，并用
**CSS `transform`** 做动画（呼吸、摇摆；鼠标移到说明文字上它会跳一下，点击画面暂停）。

```
examples/resources/pelican-bike.svg   矢量原图（720×520，44 条 path，15 个渐变 stop）
examples/gui/pelican-svg/main.ts      程序本体：开窗、加载文档、逐帧动画、打印校验报告
examples/gui/pelican-svg/index.html   文档：CSS 画廊外框 + <img src="pelican-bike.svg">
examples/gui/pelican-svg/xbintsc.config.json  入口 + 扩展（gui、node），所以不需要任何命令行开关
<输出目录>/pelican-bike.svg           构建后拷过来的副本，和 exe 放在一起
```

预览就直接看 `examples/resources/pelican-bike.svg`（GitHub 会渲染它）。这里以前放过一张 720×520 的
PNG 预览，删掉了：它只是给人看的，编译和运行都不需要它。

## 图和程序是怎么接上的

和网页里显示一张图完全一样，`index.html` 里用**相对它自己**的地址：

```html
<img id="art" src="pelican-bike.svg" alt="鹈鹕骑自行车">
```

也就是说：**图要和窗口程序在一起**。构建后把矢量原图拷到 exe 旁边，双击 `pelican-svg.exe` 就能显示
（这和普通桌面程序的习惯一致）：

```bat
copy examples\resources\pelican-bike.svg build\pelican-svg\pelican-bike.svg
```

**只有原生引擎用不了相对地址**：引擎没有“文档基地址”的概念，`xt_image_load` 是按**进程工作目录**
打开的；而编译后的程序里 `import.meta` 没有值、也没有 `process.execPath` / `chdir`，所以它没法自己
推断出 `index.html` 在哪。于是 `main.ts` 替它找文件，找到后把属性改成绝对路径再 `loadHTML`。顺序是：

1. `<当前工作目录>/pelican-bike.svg` —— 双击 exe、或在示例目录里运行时的写法
2. `examples/resources/pelican-bike.svg`（以及从示例目录往上数三层的同一路径）—— 从仓库根运行时的写法

两种情况都实测可用；都找不到时报告会打 `svg-file=not found: …`，而不是假装图已经画出来了。

> 两个坑，都踩过：
>
> - **别用 `file:///D:/…` 这种 URL。** Windows 上引擎的 `file://` 处理会把第三个斜杠当成 authority
>   分隔符，最后交给系统的是 `/D:/…`，打不开（stderr 报 `could not decode image`）。给原生绝对路径最稳。
> - **`data:` URI 现在可以用。** `<img src="data:image/svg+xml;base64,…">` 与百分号编码的
>   `data:image/svg+xml,%3Csvg…` 都会解码并显示（仍受 NanoSVG 子集限制，滤镜等不渲染）。
>   本示例仍用磁盘文件，因为原图 `examples/resources/pelican-bike.svg` 还要给其它示例和外部 SVG 编辑器使用。

引擎同时支持 SVG **文件**与**内联 `<svg>` 子树**：`runtime/ext_gui/svg.cpp` 封装了 vendored 的
**NanoSVG** 解析器 + 抗锯齿扫描线光栅化器，`image.{h,cpp}` 把 SVG 当作矢量图，按**绘制尺寸**
光栅化，所以放大也依然锐利，而不是放大位图；内联子树则先由 `inline_svg.{h,cpp}` 序列化回
独立文档，再走同一条路径。本示例仍用**文件**，因为原图 `examples/resources/pelican-bike.svg`
要同时给其它示例和外部 SVG 编辑器使用，而不是内嵌在这一个 HTML 里。

`main.ts` 因此特意很短：开窗 → `loadHTML` → 在第一个呈现帧上打印一份 `label=value` 报告。报告里
`painted-image` 那一行直接来自显示列表，是“这张 SVG 真的解码并画出来了”的证据——路径写错时 CSS 仍然
会给出 720×520 的盒子，但显示列表里不会有这张图。

## 动画：CSS transform

画面不是一张静态图。`index.html` 里 `#art` 带 `transform-origin: 50% 92%`（支点
在车轮挨着地面的高度，所以摇摆看起来像鹈鹕在侧倾，而不是在转圈），`main.ts` 在
`requestAnimationFrame` 里每帧重写它的 `style.transform`：

```ts
const pose = (time: number): string => {
  const s = time / 1000;
  const bob = Math.sin(s * 2.4) * 5;          // 上下 ±5px
  const sway = Math.sin(s * 1.15) * 1.1;      // 摇摆 ±1.1°
  const breath = 1 + Math.sin(s * 2.4 + 1.1) * 0.014;
  return "translateY(" + bob.toFixed(2) + "px) rotate(" + sway.toFixed(2) + "deg) scale(" + breath.toFixed(3) + ")";
};
art.style.setProperty("transform", pose(time));
```

另一处动作用的是 CSS **transition** 而不是逐帧改写（把鼠标移到下面的说明文字上）：

```css
#caption {
  transform-origin: 50% 100%;
  transition: transform 260ms ease-out, color 260ms ease-out;
}
#caption:hover { transform: translateY(-2px) scale(1.03); color: #dbe7f4; }
#frame:hover { border-color: #3d5a80; }        /* 画框边框也走 transition */
.paused #caption { color: #5d7186; }           /* 点击画面暂停时会加上 body.paused */
```

`transform` 是**绘制期**效果：布局几何完全不参与（不重排），引擎在布局之后给每个
盒子合成一个 2D 矩阵，绘制时把每个矩形/文本/图像的四个角乘上这个矩阵。所以
`translateY(-4px)` 的抬升、`rotate()` 的旋转都不会影响兄弟节点和文档高度——这正是
网页里 `transform` 的语义。报告里的两行正好是这条链路的证据：

```
art-box=x=40 y=35 720x520            ← 布局盒（没变，仍是 CSS 说的 720x520）
art-moved-box=x=37 y=32 731x533      ← getBoundingClientRect()：变换后的包围盒
art-matrix=matrix(1.006,0.01334,-0.01334,1.006,4.486,-3.372)   ← 显示列表里的矩阵
```

## 构建与运行

需要 clang ≥ 16。Windows 上使用 Visual Studio 自带的 LLVM clang 即可，**无需**
开发者命令提示符（它会自行找到 MSVC/SDK 的头文件和库）：

```bat
:: 无需开发者命令提示符
set xbintsc_CLANG=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang.exe
npx tsx src/cli/main.ts run examples/gui/pelican-svg/main.ts
```

打包成独立可执行文件（`-o` 的父目录会被自动创建）。**构建后把矢量图拷到 exe 旁边**，这样双击就能
显示（`copy` 的目标目录换成你实际的输出目录即可）：

```bat
npx tsx src/cli/main.ts build examples/gui/pelican-svg/main.ts -o build/pelican-svg/pelican-svg.exe
copy examples\resources\pelican-bike.svg build\pelican-svg\pelican-bike.svg
build\pelican-svg\pelican-svg.exe
```

发布版 xbintsc 不需要 `npx tsx` 前缀：

```bash
xbintsc run examples/gui/pelican-svg/main.ts
```

窗口会一直开着，直到你关掉它。设 `XT_GUI_AUTOCLOSE_MS=<毫秒>` 可以让引擎到点自动关窗——无头冒烟测试
用的就是这个模式。`xbintsc.config.json` 里打开了 `app.console`，这样在 Windows 上报告还能打到控制台
（GUI 程序默认是 Windows 子系统，stdout/stderr 无人接收）；不需要那份报告时把它去掉即可。

> 从仓库根目录 `run` 时不需要拷贝：那时 `main.ts` 用的是 `examples/resources/` 里的原图。

### 为什么还需要 `node` 扩展

程序用 `readFileSync` 把 SVG 读回来，对照着打印它自己声明的尺寸/`viewBox`/path 和渐变 stop 数量
（`svg-declares`、`svg-has-viewbox`、`svg-paths`、`svg-stops`）。这是刻意加的“两边都看一眼”的校验：
显示列表证明引擎画了它，文件内容证明画的是这张图。

## 一次真实运行的输出

```
window=800x640 (gui extension, SDL_GPU)
gpu=vulkan
art-box=x=40 y=35 720x520
paint-shapes=6
painted-images=1
painted-image=image x=40.0 y=35.0 w=720.0 h=520.0 src=D:/.../examples/resources/pelican-bike.svg
hit-test=img#art
art-origin=50% 92%
svg-file=D:/.../examples/resources/pelican-bike.svg
svg-declares=720x520
svg-bytes=9959
svg-has-viewbox=true
svg-paths=44
svg-stops=15
art-pose@1=translateY(5px) rotate(0.76deg) scale(1.006, 1.006)
art-moved-box=x=37 y=32 731x533
art-matrix=matrix(1.006,0.01334,-0.01334,1.006,4.486,-3.372)
art-pose@2=translateY(5px) rotate(0.76deg) scale(1.006, 1.006)
gui closed
gui exited
```

（`D:/...` 是仓库根的真实绝对路径；显示列表里出现绝对路径，就是 `main.ts` 改写过 `src` 的痕迹。
`art-pose@…`/`art-moved-box`/`art-matrix` 里的数字与真实时间有关（来自
`requestAnimationFrame` 的时间戳），所以 e2e 测试只匹配它们的**形状**，不比对具体数值；
`art-box` 与 `hit-test`/`svg-*` 才是确定性的断言点。）

`gpu=` 是 `win.driver()`：SDL_GPU 会在设备支持的 Vulkan / Direct3D 12 / Metal 里挑一个，引擎为每种
后端都带了对应的着色器格式。

## 画图时踩到的两个坑

- **渐变坐标要写百分号。** 引擎里的 NanoSVG 把 `gradientUnits` 默认成 `objectBoundingBox`，而**不带单位
  的数字按百分比解释**：`y2="1"` 是 1%，不是整幅高度，于是整块区域只剩下一条细细的渐变色带，其余是纯色。
  写 `y1="0%" y2="100%"` 之后，浏览器和引擎的表现才一致。这也是 `pelican-bike.svg` 里所有渐变都用百分号的原因。
- **`transform` 是绘制期的，所以它不能把图片“解码得更大”。** SVG 按**绘制尺寸**光栅化，
  而 `scale(1.014)` 之后引擎仍复用那张位图纹理再缩放顶点（引擎不会因为矩阵变化而重新光栅化，
  因为那会每帧重做一次矢量渲染）；因此呼吸幅度刻意很小，放大很多倍时矢量图会显得软。想放大多倍请看
  `examples/gui/pelican-bike` 那种用 CSS 盒子画的版本，它是纯几何绘制、任意缩放都清晰。

## 相关文档

- [doc/gui/media.md](../../../doc/gui/media.md) — 图片与 SVG 的加载、内在尺寸、矢量按绘制尺寸缓存
- [doc/gui/layout.md](../../../doc/gui/layout.md#变换m15) — `transform`/`transform-origin` 的合成、命中测试与包围盒
- [doc/gui/animation.md](../../../doc/gui/animation.md#css-transformm15) — 逐帧姿态与 `transition: transform`
- [doc/gui/known-issues.md](../../../doc/gui/known-issues.md) — SVG 受限于 NanoSVG 的哪些语法
- [doc/ai/extensions.md](../../../doc/ai/extensions.md) — `gui` / `node` 扩展
