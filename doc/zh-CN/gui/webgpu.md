# WebGPU（`<canvas>` 上的 `navigator.gpu`）

> 属于 [GUI 扩展](../gui.md)；引擎总览、既定决策与路线图见该页。

页面可以创建 WebGPU 设备并绘制到 `<canvas>`：

```ts
const canvas: any = document.getElementById("scene");
const context: any = canvas.getContext("webgpu");
const gpu: any = (navigator as any).gpu;
const adapter = await gpu.requestAdapter();
const device = await adapter.requestDevice();
context.configure({ device, format: gpu.getPreferredCanvasFormat() });

const module = device.createShaderModule({ code: WGSL });
// … pipeline、bind group、command encoder、render pass …
```

实现位于可选的 **`webgpu` 链接特性**中（依赖 `gpu`，即内置的 glslang）。
完全不用 WebGPU 的页面不会链接其中任何代码：`getContext("webgpu")` 返回
`null`，`navigator.gpu` 为 `undefined`，`GPU…` 宿主类型也不存在，因此
`typeof` 探测会走回退路径。

## 一帧如何到达屏幕

- `context.getCurrentTexture()` 返回的 `GPUTexture` 同时就是画布的颜色纹理，
  页面图像管线直接采样它（不经 CPU 回读），与 WebGL 路径一致。
- `context.configure()` 按画布的 `width`/`height` 创建后备缓冲；
  `canvas.width`/`canvas.height` 的赋值会重建它。
- 队列提交即提交 SDL_GPU 命令缓冲。已结束的 render pass 在下一帧对合成器可见。
- 作为**uniform buffer**使用的缓冲区保留一份 CPU 镜像：SDL_GPU 通过
  `SDL_Push…UniformData` 接收 uniform 数据而非绑定缓冲；`writeBuffer`
  同时更新镜像与（有 GPU 用途时的）SDL 缓冲。

## WGSL

WGSL 先翻译为 Vulkan 方言的 GLSL，再交给与 WebGL 相同的内置 glslang 编译；
在 Metal 上再由内置 SPIRV-Cross 转成 MSL。前端
（`runtime/ext_gui/wgsl_*.cpp`）支持：

- `struct`、`alias`、`const`、`override`，以及 `private`、`workgroup`、
  `uniform`、`storage` 地址空间的模块级 `var`；
- `@vertex`、`@fragment`、`@compute` 入口点，以及 `@location`、
  `@builtin(position|vertex_index|instance_index|front_facing|frag_depth|
  sample_index|sample_mask|local_invocation_id|global_invocation_id|
  workgroup_id|num_workgroups|local_invocation_index)`、`@interpolate`、
  `@group`/`@binding`、`@workgroup_size`；
- 标量/向量/矩阵/数组构造器、swizzle、`let`/`var`、`if`/`else`、`for`、
  `while`、`loop`/`continuing`、`switch`、`break`、`continue`、`return`、
  `discard`，以及常用数学/纹理内建函数；
- `texture_2d`/`texture_2d_array`/`texture_3d`/`texture_cube`（含 depth 变体）、
  `sampler`、`sampler_comparison`、`texture_storage_2d`，以及
  `textureSample*`、`textureLoad`、`textureStore`、`textureDimensions`、
  `textureGather`。

WGSL 中纹理与采样器是两个独立绑定，而 SDL_GPU 成对绑定，因此翻译器会分析
`textureSample` 调用点，为每个（纹理, 采样器）对生成一个组合采样器；仅通过
`textureLoad` 使用的纹理也会获得一个带默认采样器的槽位。

## 已实现的 API

已实现接口：`GPU`、`GPUAdapter`、`GPUAdapterInfo`、`GPUDevice`、`GPUQueue`、
`GPUBuffer`、`GPUTexture`、`GPUTextureView`、`GPUSampler`、`GPUShaderModule`、
`GPUBindGroupLayout`、`GPUPipelineLayout`、`GPUBindGroup`、
`GPURenderPipeline`、`GPUComputePipeline`、`GPUCommandEncoder`、
`GPURenderPassEncoder`、`GPUComputePassEncoder`、`GPURenderBundleEncoder`、
`GPURenderBundle`、`GPUQuerySet`、`GPUCanvasContext`、`GPUCompilationInfo`、
`GPUCompilationMessage`，以及 `GPUBufferUsage`/`GPUTextureUsage`/
`GPUShaderStage`/`GPUMapMode`/`GPUColorWrite` 命名空间。

包括设备创建、错误作用域（`pushErrorScope`/`popErrorScope`）与
`uncapturederror`、缓冲区映射（`mapAsync`/`getMappedRange`/`unmap`）、
`queue.writeBuffer`/`writeTexture`、render/compute pipeline（含 `*Async`）、
bind group、render/compute pass、绘制（索引、间接、实例化）、compute dispatch
（直接与间接）、`copyBufferToBuffer`、`copyBufferToTexture`、
`copyTextureToBuffer`、`copyTextureToTexture`、`clearBuffer`，以及基础的
render bundle 回放。

## 未实现

以下规范内容可以调用，但会记录一条校验错误（或无可观察行为），因为
SDL_GPU 没有对应能力：

- **查询**：`GPUQuerySet` 存在，但 `beginOcclusionQuery`、
  `endOcclusionQuery`、`resolveQuerySet`、`writeTimestamp` 会记录校验错误；
  该 SDL_GPU 构建没有查询 API。
- **`copyExternalImageToTexture` / `importExternalTexture`**：DOM 图像/画布来源
  不会桥接到 WebGPU 队列。
- **原子操作**（`atomic<u32>` 与 `atomic*` 内建）被 WGSL 前端拒绝。
- **片元阶段的存储纹理写入**、超出 SDL 颜色目标 resolve 的 MSAA、以及非
  `2d` 画布上下文。
- 用户函数中的**指针**（`ptr<…>` 参数、局部变量的 `&`/`*`）。
- 每个阶段最多 **4 个 uniform buffer**、**16 个采样纹理**（SDL_GPU 每阶段
  限制）；超出的着色器会得到带说明的校验失败。
- `GPUTexture.createView()` 接受描述对象，但 SDL_GPU 绑定整张纹理，因此
  `baseMipLevel`/`arrayLayerCount` 选择不会被表示。
- 每个 bind group 只把第一个动态偏移应用到所有动态项；常见的“每组一个偏移”
  用法可以正常工作。

Direct3D 12 不可用：SDL_GPU 的 D3D12 后端只接受 DXIL，本构建没有对应编译器。
Windows 上 GUI 引擎在可用时选择 Vulkan（与 WebGL 相同的规则）。

## 测试

`tests/e2e/gui-webgpu.test.ts` 用 WGSL 顶点/片元着色器绘制带纹理的三角形，
通过 `copyTextureToBuffer` 回读画布，对存储缓冲运行 compute shader，并检查
错误作用域与着色器编译消息。示例：`examples/gui/webgpu-triangle`。
