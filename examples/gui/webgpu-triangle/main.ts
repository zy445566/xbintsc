// WebGPU 三角形 — a WGSL triangle rendered through `navigator.gpu`.
//
// The gui extension implements the WebGPU API over SDL_GPU (Vulkan on
// Windows/Linux, Metal on macOS). The WGSL vertex/fragment pair is translated
// to GLSL and compiled by the bundled glslang; the page composites the
// canvas's colour texture directly, with no readback.
//
//   npx tsx src/cli/main.ts run examples/gui/webgpu-triangle/main.ts
//   npx tsx src/cli/main.ts build examples/gui/webgpu-triangle/main.ts
//
// Set XT_GUI_AUTOCLOSE_MS=<ms> to close the window after a delay.

import { createWindow } from "gui";
import page from "./index.html";

const WIDTH = 640;
const HEIGHT = 640;

const SHADER = `
struct Uniforms {
  color: vec4f,
};

@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) color: vec3f,
};

@vertex
fn vertexMain(@builtin(vertex_index) index: u32) -> VertexOut {
  var positions = array<vec2f, 3>(
    vec2f(0.0, 0.6),
    vec2f(-0.6, -0.5),
    vec2f(0.6, -0.5),
  );
  var colors = array<vec3f, 3>(
    vec3f(1.0, 0.25, 0.25),
    vec3f(0.25, 1.0, 0.25),
    vec3f(0.25, 0.35, 1.0),
  );
  var out: VertexOut;
  out.position = vec4f(positions[index], 0.0, 1.0);
  out.color = colors[index];
  return out;
}

@fragment
fn fragmentMain(in: VertexOut) -> @location(0) vec4f {
  return vec4f(in.color, 1.0) * u.color;
}
`;

const win = createWindow({ title: "WebGPU 三角形 · WebGPU Triangle", width: WIDTH, height: HEIGHT });
win.setBackground("#05070b");

const line = (label: string, value: any): void => console.log(label + "=" + value);

const run = async (): Promise<void> => {
  const canvas: any = document.getElementById("scene");
  const context: any = canvas.getContext("webgpu");
  const gpu: any = (navigator as any).gpu;
  line("webgpu", context != null && gpu != null);
  if (context == null || gpu == null) {
    line("reason", "no WebGPU backend on this host");
    win.close();
    return;
  }

  const adapter = await gpu.requestAdapter();
  const device: any = await adapter.requestDevice();
  const format = gpu.getPreferredCanvasFormat();
  line("driver", win.driver());
  line("format", format);

  context.configure({
    device,
    format,
    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
  });

  const module: any = device.createShaderModule({ code: SHADER });
  const info = await module.getCompilationInfo();
  line("messages", info.messages.length);

  const pipeline: any = device.createRenderPipeline({
    vertex: { module, entryPoint: "vertexMain", buffers: [] },
    fragment: {
      module,
      entryPoint: "fragmentMain",
      targets: [{ format }],
    },
    primitive: { topology: "triangle-list" },
  });

  const uniform = device.createBuffer({
    size: 16,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  device.queue.writeBuffer(uniform, 0, new Float32Array([1, 1, 1, 1]));

  const bindGroup = device.createBindGroup({
    layout: pipeline.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: uniform } }],
  });

  const encoder: any = device.createCommandEncoder();
  const pass: any = encoder.beginRenderPass({
    colorAttachments: [{
      view: context.getCurrentTexture().createView(),
      clearValue: { r: 0.02, g: 0.03, b: 0.06, a: 1 },
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

  /* Read the centre pixel back to prove the draw reached the canvas texture. */
  const readback: any = device.createBuffer({
    size: 256,
    usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
  });
  const copy: any = device.createCommandEncoder();
  copy.copyTextureToBuffer(
    { texture: context.getCurrentTexture(), origin: { x: 256, y: 300 } },
    { buffer: readback, bytesPerRow: 256 },
    { width: 1, height: 1 },
  );
  device.queue.submit([copy.finish()]);
  await readback.mapAsync(GPUMapMode.READ);
  const pixels = new Uint8Array(readback.getMappedRange());
  line("center", pixels[0] + "," + pixels[1] + "," + pixels[2] + "," + pixels[3]);
  readback.unmap();
  line("ok", true);

};

win.on("ready", () => { run(); });
win.loadHTML(page);
