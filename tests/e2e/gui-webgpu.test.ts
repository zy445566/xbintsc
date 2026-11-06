/**
 * End-to-end test for the `gui` extension's WebGPU surface.
 *
 * The program asks `navigator.gpu` for an adapter/device, configures a
 * `<canvas>` context, renders a textured triangle with a WGSL vertex/fragment
 * pair, reads the canvas back through `copyTextureToBuffer`, then runs a WGSL
 * compute shader over a storage buffer. A pass proves the WGSL front end, the
 * pipeline/bind-group mapping onto SDL_GPU, the canvas compositing path and the
 * buffer mapping/readback path all work.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgpu</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

const INSTANCED_PAGE = `
<html><head><title>webgpu instanced</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGPU", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("renders with WGSL, reads the canvas back and dispatches compute", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgpu",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "webgpu", width: 320, height: 240 });

      const label = (name: string, value: any): void => console.log(name + "=" + value);
      const rgba = (pixels: any): string =>
        pixels[0] + "," + pixels[1] + "," + pixels[2] + "," + pixels[3];

      const RENDER = \`
struct U { color: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;

struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
};

@vertex
fn vs(@builtin(vertex_index) index: u32) -> VOut {
  var positions = array<vec2f, 3>(
    vec2f(-1.0, -1.0),
    vec2f(3.0, -1.0),
    vec2f(-1.0, 3.0),
  );
  var uvs = array<vec2f, 3>(
    vec2f(0.0, 1.0),
    vec2f(2.0, 1.0),
    vec2f(0.0, -1.0),
  );
  var out: VOut;
  out.pos = vec4f(positions[index], 0.0, 1.0);
  out.uv = uvs[index];
  return out;
}

@fragment
fn fs(in: VOut) -> @location(0) vec4f {
  return textureSample(tex, samp, in.uv) * u.color;
}
\`;

      const COMPUTE = \`
@group(0) @binding(0) var<storage, read_write> data: array<f32>;

@compute @workgroup_size(4)
fn main(@builtin(global_invocation_id) id: vec3u) {
  let i = id.x;
  for (var k = 0; k < 2; k = k + 1) {
    data[i] = data[i] * 2.0 + 1.0;
  }
}
\`;

      const run = async (): Promise<void> => {
        const canvas: any = document.getElementById("c");
        const context: any = canvas.getContext("webgpu");
        label("context", context != null);
        const gpu: any = (navigator as any).gpu;
        label("gpu", gpu != null);
        if (gpu == null || context == null) {
          win.close();
          return;
        }
        const adapter = await gpu.requestAdapter();
        label("adapter", adapter != null);
        const device: any = await adapter.requestDevice();
        label("device", device != null);
        label("format", gpu.getPreferredCanvasFormat());

        const usage = GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC;
        context.configure({ device, format: gpu.getPreferredCanvasFormat(), usage });
        label("configured", context.getConfiguration() != null);

        const module: any = device.createShaderModule({ code: RENDER });
        const compiled = await module.getCompilationInfo();
        label("compiled", compiled.messages.length);

        const pipeline: any = device.createRenderPipeline({
          vertex: {
            module,
            entryPoint: "vs",
            buffers: [],
          },
          fragment: {
            module,
            entryPoint: "fs",
            targets: [{ format: gpu.getPreferredCanvasFormat() }],
          },
          primitive: { topology: "triangle-list" },
        });
        label("pipeline", pipeline != null);

        const uniform = device.createBuffer({
          size: 16,
          usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });
        device.queue.writeBuffer(uniform, 0, new Float32Array([1, 0, 0, 1]));

        const texture: any = device.createTexture({
          size: { width: 2, height: 2 },
          format: "rgba8unorm",
          usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
        });
        device.queue.writeTexture(
          { texture },
          new Uint8Array([
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255,
          ]),
          { bytesPerRow: 8 },
          { width: 2, height: 2 },
        );
        const view = texture.createView();
        const sampler = device.createSampler({
          magFilter: "nearest",
          minFilter: "nearest",
          addressModeU: "clamp-to-edge",
          addressModeV: "clamp-to-edge",
        });

        const bindGroup = device.createBindGroup({
          layout: pipeline.getBindGroupLayout(0),
          entries: [
            { binding: 0, resource: { buffer: uniform } },
            { binding: 1, resource: view },
            { binding: 2, resource: sampler },
          ],
        });
        label("bind-group", bindGroup != null);

        const encoder: any = device.createCommandEncoder();
        const pass: any = encoder.beginRenderPass({
          colorAttachments: [{
            view: context.getCurrentTexture().createView(),
            clearValue: { r: 0, g: 1, b: 0, a: 1 },
            loadOp: "clear",
            storeOp: "store",
          }],
        });
        pass.setPipeline(pipeline);
        pass.setBindGroup(0, bindGroup);
        pass.draw(3, 1, 0, 0);
        pass.end();
        device.queue.submit([encoder.finish()]);
        await device.queue.onSubmittedWorkDone();
        /* The canvas texture must appear in the compositor's display list, not
         * just in the GPU readback: the engine publishes a WebGPU canvas's
         * colour-texture id during its canvas sync. */
        label("composite", String((win as any).paintList()).indexOf("canvas=") >= 0);

        const readback: any = device.createBuffer({
          size: 256,
          usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
        });
        const copy: any = device.createCommandEncoder();
        copy.copyTextureToBuffer(
          { texture: context.getCurrentTexture(), origin: { x: 16, y: 16 } },
          { buffer: readback, bytesPerRow: 256 },
          { width: 1, height: 1 },
        );
        device.queue.submit([copy.finish()]);
        await readback.mapAsync(GPUMapMode.READ);
        const pixels = new Uint8Array(readback.getMappedRange());
        label("triangle", rgba(pixels));
        readback.unmap();
        readback.destroy();

        device.pushErrorScope("validation");
        device.createTexture({
          size: { width: 0, height: 1 },
          format: "rgba8unorm",
          usage: GPUTextureUsage.TEXTURE_BINDING,
        });
        const captured: any = await device.popErrorScope();
        label("validation", captured != null);

        const broken: any = device.createShaderModule({ code: "@vertex fn bad( {" });
        const brokenInfo = await broken.getCompilationInfo();
        label("compile-error", brokenInfo.messages.length > 0);

        const computeModule: any = device.createShaderModule({ code: COMPUTE });
        const computePipeline: any = device.createComputePipeline({
          compute: { module: computeModule, entryPoint: "main" },
        });
        label("compute-pipeline", computePipeline != null);
        const storage = device.createBuffer({
          size: 16,
          usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
        });
        device.queue.writeBuffer(storage, 0, new Float32Array([1, 2, 3, 4]));
        const computeGroup = device.createBindGroup({
          layout: computePipeline.getBindGroupLayout(0),
          entries: [{ binding: 0, resource: { buffer: storage } }],
        });
        const computeEncoder: any = device.createCommandEncoder();
        const computePass: any = computeEncoder.beginComputePass();
        computePass.setPipeline(computePipeline);
        computePass.setBindGroup(0, computeGroup);
        computePass.dispatchWorkgroups(1, 1, 1);
        computePass.end();
        device.queue.submit([computeEncoder.finish()]);
        await device.queue.onSubmittedWorkDone();

        const result: any = device.createBuffer({
          size: 16,
          usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
        });
        const copyCompute: any = device.createCommandEncoder();
        copyCompute.copyBufferToBuffer(storage, 0, result, 0, 16);
        device.queue.submit([copyCompute.finish()]);
        await result.mapAsync(GPUMapMode.READ);
        const values = new Float32Array(result.getMappedRange());
        label("compute", values[0] + "," + values[1] + "," + values[2] + "," + values[3]);
        result.unmap();

        win.close();
      };

      win.on("ready", () => { run(); });
      win.loadHTML(page);
      console.log("loaded");
      `,
      { files: { "page.html": PAGE }, autocloseMs: 12000, console: true },
    );

    expect(value("context")).toBe("true");
    expect(value("gpu")).toBe("true");
    expect(value("adapter")).toBe("true");
    expect(value("device")).toBe("true");
    expect(value("format")).toBe("bgra8unorm");
    expect(value("configured")).toBe("true");
    expect(value("compiled")).toBe("0");
    expect(value("pipeline")).toBe("true");
    expect(value("bind-group")).toBe("true");
    expect(value("composite")).toBe("true");
    expect(value("triangle")).toMatch(/^0,0,[1-9][0-9]*,255$/);
    expect(value("validation")).toBe("true");
    expect(value("compile-error")).toBe("true");
    expect(value("compute-pipeline")).toBe("true");
    expect(value("compute")).toBe("7,11,15,19");
    expect(stderr).not.toContain("SDL_GPU");
  });

  it("binds a second, per-instance vertex buffer without clobbering slot 0", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgpu-instanced",
      `
      import { createWindow } from "gui";
      import page from "./instanced.html";

      const win = createWindow({ title: "instanced", width: 256, height: 256 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      const SHADER = \`
struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) color: vec3f,
};

@vertex
fn vs(@location(0) position: vec2f, @location(1) instance: vec4f) -> VOut {
  var out: VOut;
  out.pos = vec4f(position.x * 0.35 + instance.x, position.y * 0.35 + instance.y, 0.0, 1.0);
  out.color = vec3f(1.0 - instance.z, instance.z, 0.0);
  return out;
}

@fragment
fn fs(in: VOut) -> @location(0) vec4f {
  return vec4f(in.color, 1.0);
}
\`;

      const run = async (): Promise<void> => {
        const canvas: any = document.getElementById("c");
        const context: any = canvas.getContext("webgpu");
        const gpu: any = (navigator as any).gpu;
        if (context == null || gpu == null) { win.close(); return; }
        const adapter = await gpu.requestAdapter();
        const device: any = await adapter.requestDevice();
        const format = gpu.getPreferredCanvasFormat();
        context.configure({
          device,
          format,
          usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
        });

        const module: any = device.createShaderModule({ code: SHADER });
        const pipeline: any = device.createRenderPipeline({
          vertex: {
            module,
            entryPoint: "vs",
            buffers: [
              {
                arrayStride: 8,
                stepMode: "vertex",
                attributes: [{ shaderLocation: 0, offset: 0, format: "float32x2" }],
              },
              {
                arrayStride: 16,
                stepMode: "instance",
                attributes: [{ shaderLocation: 1, offset: 0, format: "float32x4" }],
              },
            ],
          },
          fragment: { module, entryPoint: "fs", targets: [{ format }] },
          primitive: { topology: "triangle-list" },
        });

        const vertices = device.createBuffer({
          size: 24,
          usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
        });
        device.queue.writeBuffer(vertices, 0, new Float32Array([-1, -1, 1, -1, 0, 1]));
        const instances = device.createBuffer({
          size: 32,
          usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
        });
        /* x/y offset and a red/green switch per instance. */
        device.queue.writeBuffer(
          instances,
          0,
          new Float32Array([-0.5, 0, 0, 0, 0.5, 0, 1, 0]),
        );

        const encoder: any = device.createCommandEncoder();
        const pass: any = encoder.beginRenderPass({
          colorAttachments: [{
            view: context.getCurrentTexture().createView(),
            clearValue: { r: 0, g: 0, b: 1, a: 1 },
            loadOp: "clear",
            storeOp: "store",
          }],
        });
        pass.setPipeline(pipeline);
        pass.setVertexBuffer(0, vertices);
        pass.setVertexBuffer(1, instances);
        pass.draw(3, 2, 0, 0);
        pass.end();
        device.queue.submit([encoder.finish()]);
        await device.queue.onSubmittedWorkDone();

        const readback: any = device.createBuffer({
          size: 256 * 64,
          usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
        });
        const copy: any = device.createCommandEncoder();
        copy.copyTextureToBuffer(
          { texture: context.getCurrentTexture(), origin: { x: 0, y: 0 } },
          { buffer: readback, bytesPerRow: 256 },
          { width: 64, height: 64 },
        );
        device.queue.submit([copy.finish()]);
        await readback.mapAsync(GPUMapMode.READ);
        const pixels = new Uint8Array(readback.getMappedRange());
        const at = (x: number, y: number): string => {
          const i = y * 256 + x * 4;
          return pixels[i] + "," + pixels[i + 1] + "," + pixels[i + 2] + "," + pixels[i + 3];
        };
        label("left", at(16, 32));
        label("right", at(48, 32));
        readback.unmap();
        win.close();
      };

      win.on("ready", () => { run(); });
      win.loadHTML(page);
      `,
      { files: { "instanced.html": INSTANCED_PAGE }, autocloseMs: 12000, console: true },
    );

    /* bgra8unorm readback: red = 0,0,255 and green = 0,255,0. A clobbered
     * slot 0 makes the instance buffer the position source, so neither probe
     * sees its triangle. */
    expect(value("left")).toBe("0,0,255,255");
    expect(value("right")).toBe("0,255,0,255");
    expect(stderr).not.toContain("SDL_GPU");
  });
});
