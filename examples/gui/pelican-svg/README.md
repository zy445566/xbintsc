# 鹈鹕骑自行车（SVG）— Pelican Bike as vector art

一只骑自行车的鹈鹕，画成 **SVG 矢量图**，由 `gui` 扩展在原生窗口里显示。

```
examples/resources/pelican-bike.svg   矢量原图（720×520，44 条 path，15 个渐变 stop）
examples/gui/pelican-svg/main.ts      程序本体：开窗、加载文档、打印校验报告
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
> - **`data:` URI 不行。** 引擎会先按扩展名或文件头给 `src` 分类（这一步要打开文件），`stb_image` 也
>   读不了 `data:`，实测 `<img src="data:image/svg+xml;base64,…">` 得到 0×0 的盒子、显示列表里没有图。
>   所以图必须是磁盘上的真实文件。

引擎**不支持内联 `<svg>` 子树**（HTML 解析器只认元素和属性、布局只认盒子、绘制只认矩形/文本/图像），
但支持 SVG **文件**：`runtime/ext_gui/svg.cpp` 封装了 vendored 的 **NanoSVG** 解析器 + 抗锯齿扫描线光栅化器，
`image.{h,cpp}` 把 SVG 当作矢量图，按**绘制尺寸**光栅化，所以放大也依然锐利，而不是放大位图。
本示例要的正是这条路径。

`main.ts` 因此特意很短：开窗 → `loadHTML` → 在第一个呈现帧上打印一份 `label=value` 报告。报告里
`painted-image` 那一行直接来自显示列表，是“这张 SVG 真的解码并画出来了”的证据——路径写错时 CSS 仍然
会给出 720×520 的盒子，但显示列表里不会有这张图。

## 构建与运行

需要 clang ≥ 16，并且 **Windows 上要有 MSVC/SDK 环境**（clang 要能找到系统头文件和库）：

```bat
:: 在 “x64 Native Tools Command Prompt for VS 2022” 里执行
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
window=760x600 (gui extension, SDL_GPU)
gpu=vulkan
art-box=x=20 y=35 720x520
paint-shapes=6
painted-images=1
painted-image=image x=20.0 y=35.0 w=720.0 h=520.0 src=D:/.../examples/resources/pelican-bike.svg
hit-test=img#art
svg-file=D:/.../examples/resources/pelican-bike.svg
svg-declares=720x520
svg-bytes=9959
svg-has-viewbox=true
svg-paths=44
svg-stops=15
frames=1
gui closed
gui exited
```

（`D:/...` 是仓库根的真实绝对路径；显示列表里出现绝对路径，就是 `main.ts` 改写过 `src` 的痕迹。）

`gpu=` 是 `win.driver()`：SDL_GPU 会在设备支持的 Vulkan / Direct3D 12 / Metal 里挑一个，引擎为每种
后端都带了对应的着色器格式。

## 画图时踩到的两个坑

- **渐变坐标要写百分号。** 引擎里的 NanoSVG 把 `gradientUnits` 默认成 `objectBoundingBox`，而**不带单位
  的数字按百分比解释**：`y2="1"` 是 1%，不是整幅高度，于是整块区域只剩下一条细细的渐变色带，其余是纯色。
  写 `y1="0%" y2="100%"` 之后，浏览器和引擎的表现才一致。这也是 `pelican-bike.svg` 里所有渐变都用百分号的原因。
- **引擎不做 CSS transform。** 所以这幅图是完全静态的矢量画；动画版（车轮、曲柄、蹬腿）请见
  `examples/gui/pelican-bike`，那个示例是用 CSS 盒子和像素精灵画出来的，不依赖 SVG。

## 相关文档

- [doc/gui/media.md](../../../doc/gui/media.md) — 图片与 SVG 的加载、内在尺寸、矢量按绘制尺寸缓存
- [doc/gui/known-issues.md](../../../doc/gui/known-issues.md) — 内联 `<svg>` 为何不支持
- [doc/ai/extensions.md](../../../doc/ai/extensions.md) — `gui` / `node` 扩展
