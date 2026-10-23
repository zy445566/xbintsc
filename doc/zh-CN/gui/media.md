# 媒体：图像、视频与音频

> 语言 / Language：[English](../../gui/media.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

## 图像（M6a）

`runtime/ext_gui/image.{h,cpp}` 封装 vendored **stb_image** 头文件，把
PNG/JPEG/BMP/GIF/TGA 解码为 RGBA8，并带有解码缓存与仅头文件的尺寸缓存
（`stbi_info`）。路径接受 `file://` 前缀与百分号编码。
`runtime/ext_gui/svg.{h,cpp}` 在其上增加 SVG：它封装 vendored **NanoSVG**
库（解析 + 抗锯齿扫描线光栅化），按扩展名或内容识别 SVG，并产出与既有管线
相同的 RGBA8 `Image`。`stb_image` 无法解码 SVG，这正是
`<img src="logo.svg">` 能工作的原因。内联 `<svg>` 子树会先被序列化成一个
独立文档（`inline_svg.{h,cpp}`），再交给同一个解析器。

- `<img>` 是一个替换元素（`display: inline-block`）：设置了 CSS 尺寸时布局
  采用该尺寸，否则采用固有像素尺寸；当只约束一个轴时保持宽高比。构建盒树时
  从文件头（无需完整解码）读取固有尺寸；对 SVG 则来自解析后的 `width`/`height`
  或 `viewBox`（两者都缺失时回退到 300x150）。
- 绘制时为每个 `<img>` 生成一个 `PaintImage`；渲染器把每个唯一 `src` 上传到
  RGBA 纹理并绘制带纹理的四边形，把共享同一纹理的连续四边形批处理。
  **位图**按固有分辨率解码并按路径缓存；**矢量图**（SVG）按设备像素下的绘制
  尺寸（逻辑尺寸 × 窗口像素密度）光栅化并保持 viewBox 宽高比，再按该尺寸缓存，
  因此缩放或 HiDPI 得到的是清晰渲染而非放大的位图。
- **内联** `<svg>` 元素与 `<img>` 一样是替换元素，但它的来源就是 DOM 子树
  本身。`runtime/ext_gui/inline_svg.{h,cpp}` 把该子树序列化回独立 SVG 文档，
  还原 HTML 解析器会小写掉的、NanoSVG 又按大小写精确匹配的拼写（`viewBox`、
  `preserveAspectRatio`、`linearGradient`、`gradientUnits` 等）。布局先取
  `width`/`height` 属性、其次 `viewBox`、最后 300x150 作为固有尺寸；绘制把
  markup 挂在图像 op 上，渲染器按绘制设备像素尺寸，通过与 SVG 文件相同的
  NanoSVG 缓存解析并光栅化。DOM 变更会在下一次布局时重新序列化（因此
  `setAttribute` 与 `innerHTML` 都会重绘图像），markup 缓存有界
  （最近最少使用），动画文档不会让它无限增长。
- 解析结果按路径缓存；解析与光栅化器状态在 `xt_svg_shutdown`
  （`xt_image_shutdown`）中释放。

文档 CSS 只作用于 `<svg>` **盒子**——尺寸、外边距、背景、边框、`transform`、
opacity、`display`——与任何其他元素一样。盒子内部的艺术内容保持 SVG 语义：
表现属性与 SVG `<style>` 中基于 `class` 的规则由 NanoSVG 读取，而不是 HTML
层叠，所以这里的 `fill`/`stroke` 不是 CSS 属性。对布局、绘制、命中测试与事件
而言整个 `<svg>` 就是一个替换盒：其子元素没有 DOM 盒，因此点击的 `e.target`
是 `<svg>` 元素而不是某个 `<rect>`。渲染器按 markup 与尺寸缓存纹理，所以每帧
重写艺术内容的程序会为每个变体保留一张纹理；更好的做法是用 `transform`、
`opacity` 给盒子做动画，或在一小组文档之间切换。

图像仍需完成：CSS `background-image: url(...)`、`data:` URI、`object-fit`、
九宫格边框。

两种 SVG 都受限于 NanoSVG 的子集：`path`、`rect`、`circle`、`ellipse`、
`line`、`polyline`、`polygon`、组（`g`）、带线性/径向渐变的 `defs` 以及基于
`class` 的 `<style>` 规则会被光栅化；`<text>`、`<use>`、滤镜、遮罩与裁剪路径
不会渲染（见[已知问题](./known-issues.md)）。

## 视频与音频（M13）

媒体播放横跨两个扩展，因此只做界面的 GUI 程序不必为用不到的解码器付出体积：

- **`gui`**（`gui.a`）负责 `<video>`/`<audio>` 元素、DOM API、渲染器集成与 SDL
  音频 sink，但**不含 FFmpeg**。`runtime/ext_gui/media.{h,cpp}` 声明播放器接口，
  并通过媒体后端在启动时安装的函数表分发（`runtime/ext_gui/media_api.h`）。
- **`media`**（`media.a`、`runtime/ext_media/`）是可选的解码后端：
  `runtime/ext_media/media.cpp` 封装静态 vendored 的 **FFmpeg** 构建，
  `runtime/ext_media/media_bridge.c` 把它注册进引擎。用 `--ext media`（或
  `--ext gui,media`）启用。`media` 依赖 `gui`；注册表会拒绝错误的组合。

没有 `media` 扩展时，元素仍然存在、DOM API 仍可调用，但打开源会失败并触发
`error`。Windows 目前也是如此：该归档仅支持 POSIX。两条路径都没有 WebView，也
没有平台媒体框架。

- **解码。** `avformat_open_input` 打开 `src`（是路径——接受 `file://`，但与
  图像不同，媒体路径*不做*百分号解码），为每种媒体选取最佳流，软件解码器
  产出 `yuv/rgb` 帧，`sws_scale` 按编码尺寸转换为 RGBA8。支持的容器/编码即
  `scripts/build-media.ts` 的最小 FFmpeg 配置：MP4/MOV/MKV/AVI/FLV/MPEG-TS/M4V/
  ASF，MP3/OGG/WAV/FLAC/AAC；视频 H.264、HEVC、VP8、VP9、AV1、MPEG-4、MJPEG；
  音频 AAC、MP3、Opus、Vorbis、FLAC、ALAC、AC3/E-AC3、WMAv2 及原始 PCM 格式。
- **视频。** `<video>` 与 `<img>` 一样是替换元素（UA 默认 `300x150`，解码
  尺寸通过 `setMediaSize` 覆盖）。绘制产出与图像相同的 `PaintImage`；渲染器把
  每个新解码帧上传到 RGBA 纹理（`ensureMediaTexture`）并绘制带纹理四边形，
  因此由 GPU 缩放到 CSS 盒，而非 CPU 缩放。播放计时基于进程单调时钟，不是帧间隔。
- **音频。** 没有音频回调：解码器随主线程 tick 被推送，并写入
  `xtgui::AudioSink`。GUI 在 `gui_media.cpp` 中用 push 模式（`NULL` 回调）的
  `SDL_OpenAudioDeviceStream` 实现该 sink，在前方缓冲约 0.25 秒，格式为
  48 kHz 立体声 `SDL_AUDIO_F32`。`volume` 映射到 `SDL_SetAudioStreamGain`；
  音频是尽力而为，无设备时（无头 CI）跳过。把 sink 留在 `gui.a` 中意味着
  `media.a` 只依赖 FFmpeg。
- **线程。** 引擎是单线程且非线程感知，因此所有解码都在主线程的
  `xt_gui_media_tick` 内完成——没有解码线程，也没有跨线程 GC 暴露。
- **DOM API。** 元素句柄直接暴露媒体接口（见
  `runtime/ext_gui/dom_api_media.cpp` 与 `dom_api_proto.cpp` 中的原型）：
  `play()` / `pause()` / `load()`，以及 `paused` / `ended` / `duration` /
  `currentTime` / `volume` / `muted` / `loop` / `autoplay` / `videoWidth` /
  `videoHeight` / `readyState` / `src` 属性。媒体元素还会触发
  `loadedmetadata`、`play`、`pause`、`ended`、`timeupdate`（每 250 毫秒）与
  `error`，因此常见的 `addEventListener` 写法可用。
- **没有内置控件。** `<video controls>` 刻意*不*实现：元素始终渲染原始帧，
  程序用普通元素和 CSS 绘制自己的控件（进度条、按钮）。模式见
  `examples/gui/media.html`（`examples/gui/media.ts`）。
- **生命周期。** 播放器按窗口持有（`XtGuiMediaEntry`，以节点为键）。
  `xt_gui_media_sync` 清除节点已不存在的条目、在 `src` 变化时重开、并在首次
  使用时打开；`xt_gui_media_shutdown` 在窗口关闭/退出/`loadHTML` 时运行。

媒体仍需完成：`<video controls>`、音视频时钟漂移校正、硬件解码、流式协议
（仅启用 `file`）、字幕、`playbackRate` 与逐帧 `requestVideoFrameCallback`。

