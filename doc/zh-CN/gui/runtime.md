# 运行时集成与内存

> 语言 / Language：[English](../../gui/runtime.md) | **简体中文**

> 属于 [GUI 扩展](../gui.md) 的一部分；引擎总览、锁定决策与路线图在那里。

## 事件循环集成

运行时改动（已落地）：

- `int xt_loop_poll(int timeout_ms)` —— 一次 reactor 迭代；`0` 非阻塞轮询，
  `< 0` 阻塞。
- `void xt_loop_set_main(xt_main_loop_fn fn)` —— 宿主可以接管主循环。
  `xt_run_event_loop()` 会委托给它；生成的 `main` 不变。
- `xt_loop_set_main(NULL)` 恢复默认的 `select(2)` 循环。

GUI 引擎既可以通过 `xt_loop_set_main` 注册自己的循环，也可以暴露显式的
`run()`；**M2 使用显式的 `run()`**，因此窗口是 TypeScript 程序可以控制的
普通原生调用。每次 tick 它都会：

1. 为每个窗口泵送 SDL 窗口/输入事件，
2. **只在文档发生变化（或首帧）时**把解码器/画布表与 DOM 对齐，
3. 当显示列表变化时构建/上传它，并渲染每个打开的窗口（清除通道 + 图形几何
   + 文本几何），
4. 调用 `xt_loop_poll(0)` 与 `xt_drain_microtasks()`，使 socket/定时器与
   `await` 续体持续取得进展，
5. 重复直到所有窗口关闭或调用 `quit()`。

只有**确实发生了事情**帧才会呈现：DOM 变更、滚动、已排队的动画帧、正在运行的
过渡、新的视频/画布帧、expose/resize 事件，或首帧。因此静态文档不会获取
swapchain、也不会绘制，而循环仍在处理事件、定时器与 promise。expose/shown/restore
事件是唯一由窗口系统（而非文档）要求的呈现，它们在 `win.stats()` 中计为
`exposes`；取不到交换链图像而被推迟的呈现计为 `deferred`，`presents - exposes -
deferred` 才是页面自身造成的工作。调试"漏掉的失效"时可用
`XT_GUI_ALWAYS_PRESENT=1` 恢复旧的每帧提交行为。`XT_GUI_PERF=1` 会打印每一帧
做了哪些工作（见[性能](./rendering.md#性能m20)）。

`xt_loop_set_main` 钩子仍保留给希望自行掌控循环的宿主。

### 帧泵（从不调用 `run()` 的页面）

为浏览器编写的页面不会调用 `run()`；它调用 `requestAnimationFrame`，期望宿主
负责绘制。`browser_loop.cpp` 用一个 16 ms 的 `xt_set_timeout` tick 覆盖这种情形：

- 它在 `loadHTML` 时启动，而不是在第一次 `requestAnimationFrame` 时启动 ——
  `ready` 由第一次**真正呈现**的帧触发，若等待页面的回调就会死锁；
- 每个 tick 派发待处理输入，渲染每个打开的窗口（渲染会在布局前清空已排队的
  动画帧回调，因此页面可以自行停止）；
- 只有在**仍有回调**时才重新排程，空闲页面会让事件循环排空并退出进程 ——
  这正是浏览器标签页的生命周期；
- 下一次 tick 排在**帧开始 + 16 ms**，而不是帧结束后再等 16 ms：帧自身耗时不再
  叠加到间隔上，10 ms 的帧以 16 ms 周期运行（而不是 26 ms），比 16 ms 更慢的页面
  则以全速运行，而不是每帧再落后一帧的时间；
- 当 `xt_gui_run_active()` 为真（程序自己调用了 `run()`）时它会让路，避免同一
  窗口每 tick 被渲染两次；
- `XT_GUI_AUTOCLOSE_MS=n` 让帧泵在 `n` 毫秒后退出，测试用它来运行不会自行
  关闭的页面。

这使网络 I/O、定时器与 `await` 在 GUI 程序内部继续工作。

## 原生 ↔ TS 桥接

- **TS → 引擎**：直接的 `xt_gui_*` 调用 / 窗口方法。
- **引擎 → TS**：`xt_call_with_this(fn, thisValue, argc, argv)`，其中函数值
  从 TS 捕获（例如用 `win.on(...)` 注册的事件处理器）。
- **未来的页面→原生 RPC**：在没有页面 JS 时不需要；原生 TS 就是控制器。
  如果日后加入声明式层，它将使用相同的 `xt_call_*` 入口。

## 内存与 GC

GUI 程序的大部分内存不在运行时堆里：DOM 树、分离节点池、元素句柄表、布局盒树
与显示列表都是 C++，标记-清扫收集器看不到它们。`stats()`（见[诊断](./api.md)）报告其中
可能增长的部分：

| 计数器 | 稳定状态下的期望值 |
| --- | --- |
| `treeNodes` | 常量——即编写的文档 |
| `detachedRoots`、`detachedNodes` | 0；若脚本自己移除了节点并仍持有，则为常量 |
| `handles`、`handleSlots` | 常量——每个脚本仍引用的元素一个 |
| `listeners` | 常量——每个注册了监听器的元素一个 |
| `heapBytes`、`heapAllocations` | 在收集器阈值附近呈锯齿波动 |

其余都是每帧用完即释放的临时数据（盒、显示列表、顶点暂存）。若某个计数器随帧数
持续上升，那就是最该先查的泄漏特征；`tests/e2e/gui-retention.test.ts` 断言常见
变更模式不会改变这些计数。

读这些数字时有两处引擎细节需要注意：

- **谁会释放节点。** `innerHTML`/`inner`/`textContent`/`replaceChild` 会释放它们
  丢弃的子树；`removeChild`/`remove` 会保留（见[节点生命周期](./dom.md)）。因此用
  `innerHTML` 每帧重建精灵的游戏内存恒定。
- **什么仍会累积。** 用 `createElement` 创建却从未插入的节点会留在池中：收集器
  没有弱引用，引擎无法知道脚本已丢弃其句柄。脚本移除后又不再引用的节点同理。
  两者都以*存活句柄*数量为界，但每帧创建元素却不插入的程序会让池随帧数增长。
- **矢量图像按像素尺寸缓存。** 位图 `<img>` 只解码一次并按路径缓存，而 SVG 会按
  绘制设备尺寸光栅化，因此解码像素缓存与 GPU 纹理缓存会为每个不同尺寸保留一项。
  对静态布局（常见情况）是常量，对少量尺寸也很小；但每帧把同一 SVG 重排到新整数
  尺寸的程序会不断累积条目——此时宜改为动画位图，或让矢量图保持固定尺寸。

`heapBytes` 背后的收集器是 `runtime/xt_alloc.c` 中的非移动标记-清扫。它的触发
阈值跟随存活集：每次回收后阈值变为存活堆的 2 倍（下限 1 MiB），因此存活集小
就意味着回收频繁而轻量、常驻内存有界。用 `XT_GC_THRESHOLD=<bytes>` 可以固定
阈值；`XT_GC_TRACE=1` 会为每次回收向 stderr 打印一行（回收前后的堆与存活分配数、
下一次触发阈值，以及按对象类型统计的存活集）——DOM 泄漏正是这样被定位到节点池的。

