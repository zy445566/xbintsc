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
`<img src="logo.svg">` 能工作的原因。

- `<img>` 是一个替换元素（`display: inline-block`）：设置了 CSS 尺寸时布局
  采用该尺寸，否则采用固有像素尺寸；当只约束一个轴时保持宽高比。构建盒树时
  从文件头（无需完整解码）读取固有尺寸；对 SVG 则来自解析后的 `width`/`height`
  或 `viewBox`（两者都缺失时回退到 300x150）。
- 绘制时为每个 `<img>` 生成一个 `PaintImage`；渲染器把每个唯一 `src` 上传到
  RGBA 纹理并绘制带纹理的四边形，把共享同一纹理的连续四边形批处理。
  **位图**按固有分辨率解码并按路径缓存；**矢量图**（SVG）按设备像素下的绘制
  尺寸（逻辑尺寸 × 窗口像素密度）光栅化并保持 viewBox 宽高比，再按该尺寸缓存，
  因此缩放或 HiDPI 得到的是清晰渲染而非放大的位图。
- 解析结果按路径缓存；解析与光栅化器状态在 `xt_svg_shutdown`
  （`xt_image_shutdown`）中释放。

图像仍需完成：CSS `background-image: url(...)`、`data:` URI、`object-fit`、
九宫格边框与内联 `<svg>` 元素（仅支持 SVG *文件*，参见[待决问题](./known-issues.md)）。

## 视频与音频（M13）

`runtime/ext_gui/media.{h,cpp}` 封装一个静态 vendored 的 **FFmpeg** 构建，
`runtime/ext_gui/gui_media.cpp` 把 DOM 树接到它上面。`<video>` 与 `<audio>`
真正解码并播放；路径中没有 WebView，也没有平台媒体框架。

- **解码。** `avformat_open_input` 打开 `src`（是路径——接受 `file://`，但与
  图像不同，媒体路径*不做*百分号解码），为每种媒体选取最佳流，软件解码器
  产出 `yuv/rgb` 帧，`sws_scale` 按编码尺寸转换为 RGBA8。支持的容器/编码即
  `scripts/build-gui.ts` 的最小 FFmpeg 配置：MP4/MOV/MKV/AVI/FLV/MPEG-TS/M4V/
  ASF，MP3/OGG/WAV/FLAC/AAC；视频 H.264、HEVC、VP8、VP9、AV1、MPEG-4、MJPEG；
  音频 AAC、MP3、Opus、Vorbis、FLAC、ALAC、AC3/E-AC3、WMAv2 及原始 PCM 格式。
- **视频。** `<video>` 与 `<img>` 一样是替换元素（UA 默认 `300x150`，解码
  尺寸通过 `setMediaSize` 覆盖）。绘制产出与图像相同的 `PaintImage`；渲染器把
  每个新解码帧上传到 RGBA 纹理（`ensureMediaTexture`）并绘制带纹理四边形，
  因此由 GPU 缩放到 CSS 盒，而非 CPU 缩放。播放计时基于墙钟
  （`SDL_GetTicks`），不是帧间隔。
- **音频。** 没有音频回调：解码器随主线程 tick 被推送，PCM 以 push 模式
  （`NULL` 回调）通过 `SDL_OpenAudioDeviceStream` 排队，在前方缓冲约 0.25 秒，
  格式为 48 kHz 立体声 `SDL_AUDIO_F32`。`volume` 映射到
  `SDL_SetAudioStreamGain`；音频是尽力而为，无设备时（无头 CI）跳过。
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

