# 鹈鹕骑单车 3D · Pelican Ride 3D

![Pelican Ride 3D 预览](./preview.png)

黄昏的海边公路上，一只戴头盔的鹈鹕骑着自行车向前赶路。整个场景是一个
**纯 WebGL 2** 的低多边形 3D 演示：没有贴图、没有 three.js，几何体、光照、
雾、阴影和动画都在 `js/` 里手写；骑车的动作、车铃和风声则由 **Web Audio
AudioContext** 实时合成。

页面本身是一个普通浏览器页面：`index.html` 通过真正的
`<script type="module">` 加载 `js/main.js`，后者用 `import` 拉起八个模块。
xbintsc 的 `gui` 扩展会读取同一份 HTML，把模块图提前编译成本地代码，
再在窗口里加载并渲染。因此同一套源码可以在浏览器和原生窗口里运行。

## 运行

### 浏览器

ES module 不能从 `file://` 可靠加载（Chrome 会按 CORS 拒绝），在示例目录起一个
本地静态服务器即可：

```bash
cd examples/gui/pelican-3d
npx serve .                 # 或者 python -m http.server 8000
```

然后打开终端里给出的地址。浏览器要求先有一次点击或按键才会真正启动
AudioContext，所以第一次进入页面后点一下画面或按任意键即可听到声音。

### 原生窗口

在仓库根目录执行（`xbintsc.config.json` 已经声明了 `gui` 扩展）：

```bat
set xbintsc_CLANG=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang.exe
npx tsx src/cli/main.ts run examples/gui/pelican-3d/main.ts
```

构建独立可执行文件：

```bat
npx tsx src/cli/main.ts build examples/gui/pelican-3d/main.ts --out examples/gui/pelican-3d/build
examples\gui\pelican-3d\build\main.exe
```

原生窗口会自动启动音频；`main.ts` 只负责开窗、加载页面和打印一份
`label=value` 报告，动画与渲染都在模块里。

设置 `XT_GUI_AUTOCLOSE_MS=<毫秒>` 可以让窗口在指定时间后自动关闭，方便冒烟测试。

## 操作

| 输入 | 动作 |
| --- | --- |
| `Space` / `↑` | 跳跃（翅膀会张开，落地有音效） |
| `B` | 按车铃（合成钟声） |
| `W` / `→` | 加速（风声变强） |
| `S` / `←` | 刹车 |
| 鼠标拖拽 | 绕骑手旋转视角 |
| 滚轮 | 拉近 / 拉远 |
| `M` | 静音 / 恢复 |
| `R` | 重置里程和速度 |
| `P` | 暂停 / 继续 |

## 文件结构

```
index.html             页面骨架 + HUD + <script type="module" src="./js/main.js">
main.ts                原生宿主：createWindow / loadHTML / run / 报告
xbintsc.config.json    entry + gui 扩展
js/main.js             启动、输入、相机、帧循环、HUD、关节动画
js/math.js             4×4 列主序矩阵、lookAt、透视、法线矩阵、关节 IK 辅助
js/geometry.js         立方体/球/圆柱/圆锥/圆环/圆盘/网格 + MeshBuilder
js/shaders.js          GLSL ES 3.00：主光照、程序化天空、实例化、软阴影
js/renderer.js         program/VAO/VBO/实例化 VAO、绘制与统计
js/world.js            草地、公路、虚线、山丘、树、花、路桩、云
js/bike.js             车架、轮组、曲柄、脚踏、车铃、车灯
js/pelican.js          鹈鹕身体、脖子、头、喙、喉囊、头盔、围巾、翅膀
js/audio.js            AudioContext 合成器（铃声、风声、跳跃、落地、chime）
```

九个 `.js` 模块全部通过 `import` 连接：

```
main.js
  ├─ audio.js
  ├─ bike.js ────────┐
  ├─ pelican.js ─────┤
  ├─ world.js ───────┤
  ├─ renderer.js ────┼─ math.js
  │                  └─ geometry.js
  └─ shaders.js
```

## WebGL 2 里用了什么

- `canvas.getContext("webgl2")` 和 `#version 300 es` 的 GLSL ES 3.00 着色器；
- `layout(location = N) in …` 属性、VAO（`createVertexArray` /
  `bindVertexArray`）；
- 树木、花、路桩、云共用网格的 **实例化绘制**：
  `vertexAttribDivisor` + `drawElementsInstanced`；
- 顶点色 + 半球/方向光 + 暖色边缘光 + 距离雾；
- 一个全屏三角形/四边形做程序化天空（渐变、太阳和光晕）；
- 一个带径向 alpha 的 XZ 圆盘做接触阴影（混合、关闭深度写入）；
- 动态两段骨骼 IK 把橙色腿连接到转动的脚踏；
- 所有几何体都按组预先烘焙到一个 VBO/VAO 中，所以整辆车、每只轮子、
  鸟身、翅膀都各是一次 `drawElements`。

`main.ts` 会打印 `gl-version`、`glsl-version`、`fps`、`draws`、`triangles`
以及 `readPixels` 采样，方便确认 GPU 真的画出了内容。典型输出：

```
window=1100x700 (gui extension, SDL_GPU)
gpu=vulkan
webgl2=true
gl-version=WebGL 2.0 (xbintsc)
glsl-version=WebGL GLSL ES 3.00 (xbintsc)
canvas=1100x700
modules=9
frames=12
fps=85
draws=45
triangles=144832
speed=5.8
distance=0
mode=CRUISE
sound=on
audio-state=running
error=0
pixel-sky=55,115,216,255
pixel-road=123,125,125,255
pixel-center=161,149,126,255
pixel-max=255,255,255,255
```

HUD 右上角还会显示实时 FPS（原生窗口没有垂直同步，所以通常高于 60；
浏览器里则会跟随显示器刷新率）。`fps` 也出现在原生报告里。

## 声音是怎么合成的

`js/audio.js` 只使用 Web Audio 的原生节点，没有任何音频文件：

- **车铃**：4 个不同频率的 `OscillatorNode`（正弦）经由各自的增益包络叠加，
  指数衰减模拟金属余音；
- **风**：循环播放的 2 秒白噪声 buffer，经过低通 `BiquadFilterNode`，增益随
  车速变化；
- **跳跃 / 落地**：正弦振荡器的快速扫频 + 高通/低通噪声脉冲；
- **加速**：带通滤波噪声从 420 Hz 扫到 2200 Hz；
- **里程碑 / 鸟鸣**：短促的三角波音阶与高频啁啾。

浏览器在没有用户手势时会保持 `AudioContext` 为 `suspended`，页面的第一次
点击或按键会调用 `resume()`；原生窗口没有这个限制，启动时就会开声。

## 关于 gui 扩展支持

这个示例只使用了当前 xbintsc `gui` 扩展已经支持的页面能力：ES module、DOM
事件、`pointer-events`、flex 布局、`AudioContext`、WebGL 2、VAO 与实例化。
不需要修改编译器或 gui 引擎就能构建和运行；如果后续把示例改成使用新的 CSS
或 DOM API，应当先在 `runtime/ext_gui` 里补齐对应实现，再更新这个页面。

## 测试

```bash
# 仅浏览器入口的静态检查 + 原生渲染冒烟测试
xbintsc_GUI_TESTS=1 npx vitest run tests/e2e/gui-example-pelican3d.test.ts
```

测试会把示例复制到临时目录，用 `gui` 扩展编译、运行，并断言
`webgl2=true`、`frames>=12`、`fps>0`、`draws>30`、`triangles>10000` 和像素采样。

相关示例：

- [pelican-bike](../pelican-bike/README.md) —— 同一题材的 HTML/CSS 2D 游戏
- [pelican-canvas](../pelican-canvas/README.md) —— Canvas 2D 版本
- [webgl2-cubes](../webgl2-cubes/README.md) —— 最小 WebGL 2 实例化示例
