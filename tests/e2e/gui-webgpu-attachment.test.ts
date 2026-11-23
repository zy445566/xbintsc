/**
 * End-to-end test for WebGPU pass-attachment semantics that the engine used to
 * get wrong:
 *
 * - `loadOp`/`depthLoadOp`/`stencilLoadOp: "load"` must keep the contents the
 *   previous pass stored. The engine used to hand SDL a cycling target even for
 *   a LOAD pass, and SDL rotates such a target to another internal allocation,
 *   so a second pass began from undefined memory — the colour and depth of the
 *   first pass silently disappeared.
 * - A texture that no pass or copy has written yet must read as WebGPU's
 *   initial zeroes rather than uninitialised GPU memory: depth 0, stencil 0,
 *   transparent black. A first pass that LOADs it may not expose garbage.
 * - `stencilLoadOp`/`stencilStoreOp` must be honoured; the engine used to clear
 *   the stencil aspect of every pass, so a stencil written by one pass was gone
 *   in the next.
 *
 * Every probe renders to an offscreen texture and copies one texel back.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgpu-attachment</title></head>
<body style="margin:0">
  <canvas id="c" width="32" height="32"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGPU attachment load and initialisation", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("keeps loaded contents and starts fresh attachments at zero", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgpu-attachment",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "attachment", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);
      const rgba = (p: any): string => p[0] + "," + p[1] + "," + p[2] + "," + p[3];

      const RENDER = \`
struct VOut { @builtin(position) pos: vec4f };

@vertex
fn vs(@builtin(vertex_index) i: u32) -> VOut {
  var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  var o: VOut;
  o.pos = vec4f(p[i], 0.5, 1.0);
  return o;
}

@fragment
fn fs() -> @location(0) vec4f {
  return vec4f(0.0, 0.0, 1.0, 1.0);
}
\`;

      const run = async (): Promise<void> => {
        const gpu: any = (navigator as any).gpu;
        const adapter = await gpu.requestAdapter();
        const device: any = await adapter.requestDevice();
        const module: any = device.createShaderModule({ code: RENDER });

        const colorTexture = (): any => device.createTexture({
          size: { width: 32, height: 32 },
          format: "rgba8unorm",
          usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
        });
        const depthTexture = (format: string): any => device.createTexture({
          size: { width: 32, height: 32 },
          format,
          usage: GPUTextureUsage.RENDER_ATTACHMENT,
        });
        const pipeline = (format: string, compare: string, write: boolean, stencil: any): any =>
          device.createRenderPipeline({
            vertex: { module, entryPoint: "vs", buffers: [] },
            fragment: { module, entryPoint: "fs", targets: [{ format: "rgba8unorm" }] },
            primitive: { topology: "triangle-list" },
            depthStencil: {
              format,
              depthWriteEnabled: write,
              depthCompare: compare,
              stencilFront: stencil,
            },
          });

        const read = async (texture: any): Promise<any> => {
          const buffer: any = device.createBuffer({
            size: 256,
            usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
          });
          const encoder: any = device.createCommandEncoder();
          encoder.copyTextureToBuffer(
            { texture, origin: { x: 16, y: 16 } },
            { buffer, bytesPerRow: 256 },
            { width: 1, height: 1 },
          );
          device.queue.submit([encoder.finish()]);
          await buffer.mapAsync(GPUMapMode.READ);
          const pixels = new Uint8Array(buffer.getMappedRange());
          const out = [pixels[0], pixels[1], pixels[2], pixels[3]];
          buffer.unmap();
          buffer.destroy();
          return out;
        };
        const submit = async (encoder: any): Promise<void> => {
          device.queue.submit([encoder.finish()]);
          await device.queue.onSubmittedWorkDone();
        };

        /* A second pass that LOADs the green colour and the depth of 1.0 the
         * first pass stored must keep both: the draw at z=0.5 passes the depth
         * test and paints blue. */
        const color: any = colorTexture();
        const depth: any = depthTexture("depth32float");
        const encoder: any = device.createCommandEncoder();
        const clearPass: any = encoder.beginRenderPass({
          colorAttachments: [{
            view: color.createView(),
            clearValue: { r: 0, g: 1, b: 0, a: 1 },
            loadOp: "clear",
            storeOp: "store",
          }],
          depthStencilAttachment: {
            view: depth.createView(),
            depthClearValue: 1.0,
            depthLoadOp: "clear",
            depthStoreOp: "store",
          },
        });
        clearPass.end();
        const loadPass: any = encoder.beginRenderPass({
          colorAttachments: [{ view: color.createView(), loadOp: "load", storeOp: "store" }],
          depthStencilAttachment: { view: depth.createView(), depthLoadOp: "load", depthStoreOp: "store" },
        });
        loadPass.setPipeline(pipeline("depth32float", "less", true, null));
        loadPass.draw(3, 1, 0, 0);
        loadPass.end();
        await submit(encoder);
        label("load", rgba(await read(color)));

        /* A texture that was never written loads as WebGPU's zeroes. Depth 0
         * rejects a fragment at z=0.5 under "less" and accepts it under
         * "greater", so the pair pins the loaded value to 0. */
        const lessColor: any = colorTexture();
        const lessDepth: any = depthTexture("depth32float");
        const lessEncoder: any = device.createCommandEncoder();
        const lessPass: any = lessEncoder.beginRenderPass({
          colorAttachments: [{
            view: lessColor.createView(),
            clearValue: { r: 1, g: 0, b: 0, a: 1 },
            loadOp: "clear",
            storeOp: "store",
          }],
          depthStencilAttachment: {
            view: lessDepth.createView(),
            depthLoadOp: "load",
            depthStoreOp: "store",
          },
        });
        lessPass.setPipeline(pipeline("depth32float", "less", false, null));
        lessPass.draw(3, 1, 0, 0);
        lessPass.end();
        await submit(lessEncoder);
        label("fresh-less", rgba(await read(lessColor)));

        const greaterColor: any = colorTexture();
        const greaterDepth: any = depthTexture("depth32float");
        const greaterEncoder: any = device.createCommandEncoder();
        const greaterPass: any = greaterEncoder.beginRenderPass({
          colorAttachments: [{
            view: greaterColor.createView(),
            clearValue: { r: 1, g: 0, b: 0, a: 1 },
            loadOp: "clear",
            storeOp: "store",
          }],
          depthStencilAttachment: {
            view: greaterDepth.createView(),
            depthLoadOp: "load",
            depthStoreOp: "store",
          },
        });
        greaterPass.setPipeline(pipeline("depth32float", "greater", false, null));
        greaterPass.draw(3, 1, 0, 0);
        greaterPass.end();
        await submit(greaterEncoder);
        label("fresh-greater", rgba(await read(greaterColor)));

        /* The colour aspect is zero-initialised too: transparent black. */
        const blank: any = colorTexture();
        const blankEncoder: any = device.createCommandEncoder();
        const blankPass: any = blankEncoder.beginRenderPass({
          colorAttachments: [{ view: blank.createView(), loadOp: "load", storeOp: "store" }],
        });
        blankPass.end();
        await submit(blankEncoder);
        label("fresh-color", rgba(await read(blank)));

        /* Stencil 1 is stored by the first pass; the second pass LOADs it and
         * draws only where the reference matches. */
        const maskColor: any = colorTexture();
        const maskDepth: any = depthTexture("depth24plus-stencil8");
        /* depth24plus-stencil8 is a promise format (the engine backs it with
         * depth32float-stencil8); the getter must still report the name the
         * page asked for. */
        label("stencil-format", maskDepth.format);
        const maskStencil: any = { compare: "equal", passOp: "keep" };
        const maskEncoder: any = device.createCommandEncoder();
        const stencilClear: any = maskEncoder.beginRenderPass({
          colorAttachments: [{
            view: maskColor.createView(),
            clearValue: { r: 1, g: 0, b: 0, a: 1 },
            loadOp: "clear",
            storeOp: "store",
          }],
          depthStencilAttachment: {
            view: maskDepth.createView(),
            depthClearValue: 1.0,
            depthLoadOp: "clear",
            depthStoreOp: "store",
            stencilClearValue: 1,
            stencilLoadOp: "clear",
            stencilStoreOp: "store",
          },
        });
        stencilClear.end();
        const stencilLoad: any = maskEncoder.beginRenderPass({
          colorAttachments: [{ view: maskColor.createView(), loadOp: "load", storeOp: "store" }],
          depthStencilAttachment: {
            view: maskDepth.createView(),
            depthLoadOp: "load",
            depthStoreOp: "store",
            stencilLoadOp: "load",
            stencilStoreOp: "store",
          },
        });
        stencilLoad.setStencilReference(1);
        stencilLoad.setPipeline(pipeline("depth24plus-stencil8", "always", false, maskStencil));
        stencilLoad.draw(3, 1, 0, 0);
        stencilLoad.end();
        await submit(maskEncoder);
        label("stencil", rgba(await read(maskColor)));

        win.close();
      };

      win.on("ready", () => { run(); });
      win.loadHTML(page);
      console.log("loaded");
      `,
      { files: { "page.html": PAGE }, autocloseMs: 12000, console: true },
    );

    expect(value("load")).toBe("0,0,255,255");
    expect(value("fresh-less")).toBe("255,0,0,255");
    expect(value("fresh-greater")).toBe("0,0,255,255");
    expect(value("fresh-color")).toBe("0,0,0,0");
    expect(value("stencil-format")).toBe("depth24plus-stencil8");
    expect(value("stencil")).toBe("0,0,255,255");
    expect(stderr).not.toContain("SDL_GPU");
  });
});
