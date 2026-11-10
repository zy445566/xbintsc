// The WebGPU backend.
//
// It mirrors the WebGL 2 renderer call for call — `createMesh`,
// `createInstancedMesh`, `begin`, `drawSky`, `draw`, `drawInstanced`,
// `drawShadow`, `end` — so main.js runs the same scene on either backend.
//
// Two details make the port work:
//
// * every mesh is uploaded as one interleaved vertex buffer (position, normal,
//   colour) plus an index buffer, and instanced scenery adds a second vertex
//   buffer with one vec4 per instance (`stepMode: "instance"`);
// * all uniforms for a frame live in one buffer cut into 256-byte slots. Each
//   draw gets its own slot through a dynamic bind-group offset, so the same
//   bind group serves every pipeline. That is also what keeps the browser path
//   correct: WebGPU applies `queue.writeBuffer` calls at submit time, so
//   reusing one slot for all draws would show only the last model matrix.

import { WGSL_SCENE } from "./shaders-wgsl.js";
import { box, cylinder, sphere } from "./geometry.js";

/** 256 bytes is the guaranteed uniform-buffer offset alignment. */
const SLOT_BYTES = 256;
const SLOT_FLOATS = SLOT_BYTES / 4;
const SLOT_COUNT = 128;

/** Interleave position / normal / colour into one (stride 36) vertex buffer. */
function interleave(geometry) {
  const positions = geometry.positions;
  const normals = geometry.normals;
  const colors = geometry.colors;
  const count = positions.length / 3;
  const data = new Float32Array(count * 9);
  for (let i = 0; i < count; i += 1) {
    const source = i * 3;
    const target = i * 9;
    data[target] = positions[source];
    data[target + 1] = positions[source + 1];
    data[target + 2] = positions[source + 2];
    data[target + 3] = normals[source];
    data[target + 4] = normals[source + 1];
    data[target + 5] = normals[source + 2];
    if (colors === undefined || colors === null) {
      data[target + 6] = 1;
      data[target + 7] = 1;
      data[target + 8] = 1;
    } else {
      data[target + 6] = colors[source];
      data[target + 7] = colors[source + 1];
      data[target + 8] = colors[source + 2];
    }
  }
  return data;
}

function vertexLayout(instanceStep) {
  const buffers = [{
    arrayStride: 36,
    stepMode: "vertex",
    attributes: [
      { shaderLocation: 0, offset: 0, format: "float32x3" },
      { shaderLocation: 1, offset: 12, format: "float32x3" },
      { shaderLocation: 2, offset: 24, format: "float32x3" },
    ],
  }];
  if (instanceStep) {
    buffers.push({
      arrayStride: 16,
      stepMode: "instance",
      attributes: [{ shaderLocation: 3, offset: 0, format: "float32x4" }],
    });
  }
  return buffers;
}

export class WebGPURenderer {
  constructor(canvas, context, device, format, width, height) {
    this.canvas = canvas;
    this.context = context;
    this.device = device;
    this.format = format;
    this.width = width;
    this.height = height;
    this.kind = "webgpu";
    this.label = "WEBGPU";
    this.gpuLabel = "SDL_GPU";
    this.glVersion = "WebGPU (WGSL)";
    this.glslVersion = "WGSL";
    this.stats = { draws: 0, triangles: 0 };
    this.queue = device.queue;
    this.frame = new Float32Array(SLOT_FLOATS);
    this.uniform = new Float32Array(SLOT_FLOATS);
    this.slot = 0;
    this.pass = null;
    this.encoder = null;
    this.currentPipeline = null;
    this.colorView = null;
    this.viewWidth = 0;
    this.viewHeight = 0;
    this.bgra = format === "bgra8unorm" || format === "bgra8unorm-srgb";
    /* main.ts is the only pixel-probe consumer, and it runs in the native
     * host. There the SDL_GPU bridge can copy a texture region to a mapped
     * buffer synchronously; a browser keeps the probes inert. */
    this.nativeHost = String(navigator.userAgent).indexOf("xbintsc") >= 0;
  }

  static async create(canvas, width, height) {
    const context = canvas.getContext("webgpu");
    const gpu = navigator.gpu;
    if (context === null || context === undefined || gpu === null || gpu === undefined) {
      throw new Error("WebGPU is not available on this host");
    }
    const adapter = await gpu.requestAdapter();
    if (adapter === null || adapter === undefined) {
      throw new Error("no WebGPU adapter (the GPU backend needs Vulkan or Metal)");
    }
    const device = await adapter.requestDevice();
    const format = gpu.getPreferredCanvasFormat();
    context.configure({
      device: device,
      format: format,
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
    });
    const renderer = new WebGPURenderer(canvas, context, device, format, width, height);
    await renderer.init();
    return renderer;
  }

  async init() {
    const device = this.device;
    device.pushErrorScope("validation");

    let setupError = "";
    try {
      this.createPipelines();

      this.uniformBuffer = device.createBuffer({
        size: SLOT_COUNT * SLOT_BYTES,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
      });
      this.bindGroup = device.createBindGroup({
        layout: this.bindGroupLayout,
        entries: [{
          binding: 0,
          resource: { buffer: this.uniformBuffer, offset: 0, size: SLOT_BYTES },
        }],
      });

      const depth = device.createTexture({
        size: { width: this.width, height: this.height },
        format: "depth24plus",
        usage: GPUTextureUsage.RENDER_ATTACHMENT,
      });
      this.depthView = depth.createView();

      const sky = new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]);
      this.skyBuffer = device.createBuffer({
        size: sky.byteLength,
        usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
      });
      this.queue.writeBuffer(this.skyBuffer, 0, sky);

      this.unitCylinder = this.createMesh(cylinder(8));
      this.unitSphere = this.createMesh(sphere(10, 14));
      this.unitBox = this.createMesh(box());
    } catch (error) {
      setupError = String(error);
    }

    const error = await device.popErrorScope();
    if (setupError !== "") throw new Error(setupError);
    if (error !== null && error !== undefined) {
      throw new Error("WebGPU setup failed: " + (error.message ?? String(error)));
    }
  }

  createPipelines() {
    const device = this.device;
    const module = device.createShaderModule({ code: WGSL_SCENE });
    this.bindGroupLayout = device.createBindGroupLayout({
      entries: [{
        binding: 0,
        visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
        buffer: { type: "uniform", hasDynamicOffset: true },
      }],
    });
    this.pipelineLayout = device.createPipelineLayout({
      bindGroupLayouts: [this.bindGroupLayout],
    });

    const targets = [{ format: this.format }];
    const depth = {
      format: "depth24plus",
      depthWriteEnabled: true,
      depthCompare: "less-equal",
    };
    const blend = {
      color: {
        srcFactor: "src-alpha",
        dstFactor: "one-minus-src-alpha",
        operation: "add",
      },
      alpha: {
        srcFactor: "one",
        dstFactor: "one-minus-src-alpha",
        operation: "add",
      },
    };

    this.litPipeline = device.createRenderPipeline({
      layout: this.pipelineLayout,
      vertex: {
        module: module,
        entryPoint: "lit_vertex",
        buffers: vertexLayout(false),
      },
      fragment: {
        module: module,
        entryPoint: "lit_fragment",
        targets: targets,
      },
      primitive: { topology: "triangle-list", cullMode: "none" },
      depthStencil: depth,
    });
    this.instancedPipeline = device.createRenderPipeline({
      layout: this.pipelineLayout,
      vertex: {
        module: module,
        entryPoint: "instanced_vertex",
        buffers: vertexLayout(true),
      },
      fragment: {
        module: module,
        entryPoint: "instanced_fragment",
        targets: targets,
      },
      primitive: { topology: "triangle-list", cullMode: "none" },
      depthStencil: depth,
    });
    this.shadowPipeline = device.createRenderPipeline({
      layout: this.pipelineLayout,
      vertex: {
        module: module,
        entryPoint: "shadow_vertex",
        buffers: vertexLayout(false),
      },
      fragment: {
        module: module,
        entryPoint: "shadow_fragment",
        targets: [{ format: this.format, blend: blend }],
      },
      primitive: { topology: "triangle-list", cullMode: "none" },
      depthStencil: {
        format: "depth24plus",
        depthWriteEnabled: false,
        depthCompare: "less-equal",
      },
    });
    this.skyPipeline = device.createRenderPipeline({
      layout: this.pipelineLayout,
      vertex: {
        module: module,
        entryPoint: "sky_vertex",
        buffers: [{
          arrayStride: 8,
          stepMode: "vertex",
          attributes: [{ shaderLocation: 0, offset: 0, format: "float32x2" }],
        }],
      },
      fragment: {
        module: module,
        entryPoint: "sky_fragment",
        targets: targets,
      },
      primitive: { topology: "triangle-strip", cullMode: "none" },
      depthStencil: {
        format: "depth24plus",
        depthWriteEnabled: false,
        depthCompare: "always",
      },
    });

    if (this.litPipeline === undefined || this.litPipeline === null ||
        this.instancedPipeline === undefined || this.instancedPipeline === null ||
        this.shadowPipeline === undefined || this.shadowPipeline === null ||
        this.skyPipeline === undefined || this.skyPipeline === null) {
      throw new Error("WebGPU pipeline creation failed");
    }
  }

  createMesh(geometry) {
    return this.uploadMesh(geometry, null);
  }

  createInstancedMesh(geometry, instances) {
    return this.uploadMesh(geometry, instances);
  }

  uploadMesh(geometry, instances) {
    const device = this.device;
    const data = interleave(geometry);
    const vertexBuffer = device.createBuffer({
      size: data.byteLength,
      usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
    });
    this.queue.writeBuffer(vertexBuffer, 0, data);

    /* WebGPU requires every buffer size and `writeBuffer` payload to be a
     * multiple of 4 bytes. A Uint16 index array with an odd number of indices
     * (for example the 9-segment pine-tree cones in world.js) is `n * 2`
     * bytes, so pad it to the next 4-byte boundary; the extra bytes are never
     * read because `drawIndexed` uses the real `indexCount`. */
    const indexByteLength = geometry.indices.byteLength;
    const indexAligned = (indexByteLength + 3) & ~3;
    const indexBuffer = device.createBuffer({
      size: indexAligned,
      usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
    });
    if (indexAligned === indexByteLength) {
      this.queue.writeBuffer(indexBuffer, 0, geometry.indices);
    } else {
      const padded = new Uint8Array(indexAligned);
      padded.set(new Uint8Array(geometry.indices.buffer, geometry.indices.byteOffset, indexByteLength));
      this.queue.writeBuffer(indexBuffer, 0, padded);
    }

    let instanceBuffer = null;
    let count = 1;
    if (instances !== null && instances !== undefined) {
      instanceBuffer = device.createBuffer({
        size: instances.byteLength,
        usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
      });
      this.queue.writeBuffer(instanceBuffer, 0, instances);
      count = instances.length / 4;
    }

    return {
      vertexBuffer: vertexBuffer,
      indexBuffer: indexBuffer,
      instanceBuffer: instanceBuffer,
      instances: count,
      indexCount: geometry.indices.length,
      indexFormat: geometry.indices instanceof Uint32Array ? "uint32" : "uint16",
    };
  }

  begin(width, height, viewProj, camera, lightDir, fogColor, fogDensity) {
    this.stats.draws = 0;
    this.stats.triangles = 0;
    this.slot = 0;
    this.currentPipeline = null;

    const u = this.frame;
    u.fill(0);
    u.set(viewProj, 0);
    u[32] = lightDir[0];
    u[33] = lightDir[1];
    u[34] = lightDir[2];
    u[36] = camera[0];
    u[37] = camera[1];
    u[38] = camera[2];
    u[40] = fogColor[0];
    u[41] = fogColor[1];
    u[42] = fogColor[2];
    u[52] = width;
    u[53] = height;
    u[54] = fogDensity;
    u[55] = 1;

    const texture = this.context.getCurrentTexture();
    /* The native backbuffer is stable until the canvas is resized, so its view
     * is reused; a browser presents a new texture every frame. */
    if (this.colorView === null || !this.nativeHost ||
        this.viewWidth !== width || this.viewHeight !== height) {
      this.colorView = texture.createView();
      this.viewWidth = width;
      this.viewHeight = height;
    }
    this.encoder = this.device.createCommandEncoder();
    this.pass = this.encoder.beginRenderPass({
      colorAttachments: [{
        view: this.colorView,
        clearValue: { r: fogColor[0], g: fogColor[1], b: fogColor[2], a: 1 },
        loadOp: "clear",
        storeOp: "store",
      }],
      depthStencilAttachment: {
        view: this.depthView,
        depthClearValue: 1,
        depthLoadOp: "clear",
        depthStoreOp: "store",
      },
    });
  }

  end() {
    if (this.pass === null) return;
    this.pass.end();
    this.pass = null;
    const command = this.encoder.finish();
    this.encoder = null;
    this.queue.submit([command]);
  }

  /** Reserve a uniform slot and copy the frame constants into the staging array. */
  beginSlot() {
    if (this.slot >= SLOT_COUNT) {
      throw new Error("WebGPU uniform slot budget exhausted (" + SLOT_COUNT + " draws)");
    }
    const offset = this.slot * SLOT_BYTES;
    this.slot += 1;
    this.uniform.set(this.frame);
    return offset;
  }

  /** Publish the slot before the draw; SDL_GPU reads the CPU mirror at draw time. */
  commitSlot(offset) {
    this.queue.writeBuffer(this.uniformBuffer, offset, this.uniform);
  }

  bindPipeline(pipeline, offset) {
    if (this.currentPipeline !== pipeline) {
      this.pass.setPipeline(pipeline);
      this.currentPipeline = pipeline;
    }
    this.pass.setBindGroup(0, this.bindGroup, [offset]);
  }

  drawSky(width, height, time) {
    const offset = this.beginSlot();
    this.uniform[52] = width;
    this.uniform[53] = height;
    this.uniform[58] = time;
    this.commitSlot(offset);
    this.bindPipeline(this.skyPipeline, offset);
    this.pass.setVertexBuffer(0, this.skyBuffer);
    this.pass.draw(4, 1, 0, 0);
    this.stats.draws += 1;
    this.stats.triangles += 2;
  }

  draw(mesh, model, normalScale, tint, alpha) {
    const ns = normalScale === undefined ? [1, 1, 1] : normalScale;
    const c = tint === undefined ? [1, 1, 1] : tint;
    const a = alpha === undefined ? 1 : alpha;

    const offset = this.beginSlot();
    this.uniform.set(model, 16);
    this.uniform[44] = c[0];
    this.uniform[45] = c[1];
    this.uniform[46] = c[2];
    this.uniform[48] = ns[0];
    this.uniform[49] = ns[1];
    this.uniform[50] = ns[2];
    this.uniform[55] = a;
    this.commitSlot(offset);

    this.bindPipeline(this.litPipeline, offset);
    this.pass.setVertexBuffer(0, mesh.vertexBuffer);
    this.pass.setIndexBuffer(mesh.indexBuffer, mesh.indexFormat, 0);
    this.pass.drawIndexed(mesh.indexCount, 1, 0, 0, 0);
    this.stats.draws += 1;
    this.stats.triangles += mesh.indexCount / 3;
  }

  drawInstanced(mesh, scroll, range) {
    const offset = this.beginSlot();
    this.uniform[56] = scroll;
    this.uniform[57] = range;
    this.commitSlot(offset);

    this.bindPipeline(this.instancedPipeline, offset);
    this.pass.setVertexBuffer(0, mesh.vertexBuffer);
    this.pass.setVertexBuffer(1, mesh.instanceBuffer);
    this.pass.setIndexBuffer(mesh.indexBuffer, mesh.indexFormat, 0);
    this.pass.drawIndexed(mesh.indexCount, mesh.instances, 0, 0, 0);
    this.stats.draws += 1;
    this.stats.triangles += (mesh.indexCount / 3) * mesh.instances;
  }

  drawShadow(mesh, model, alpha) {
    const offset = this.beginSlot();
    this.uniform.set(model, 16);
    this.uniform[55] = alpha;
    this.commitSlot(offset);

    this.bindPipeline(this.shadowPipeline, offset);
    this.pass.setVertexBuffer(0, mesh.vertexBuffer);
    this.pass.setIndexBuffer(mesh.indexBuffer, mesh.indexFormat, 0);
    this.pass.drawIndexed(mesh.indexCount, 1, 0, 0, 0);
    this.stats.draws += 1;
    this.stats.triangles += mesh.indexCount / 3;
  }

  /**
   * Read a small canvas region back to the CPU, synchronously.
   *
   * A native `copyTextureToBuffer` completes before it returns, so a buffer
   * created with `mappedAtCreation` already holds the pixels: no `mapAsync`, no
   * promise and no full-frame capture. Keeping the readback this small and on
   * demand is what keeps the animation loop from stalling.
   */
  readRegion(x, y, width, height) {
    if (!this.nativeHost) return null;
    if (width <= 0 || height <= 0) return null;
    const rowBytes = width * 4;
    const stride = Math.ceil(rowBytes / 256) * 256;
    const readback = this.device.createBuffer({
      size: stride * height,
      usage: GPUBufferUsage.COPY_DST,
      mappedAtCreation: true,
    });
    const encoder = this.device.createCommandEncoder();
    encoder.copyTextureToBuffer(
      { texture: this.context.getCurrentTexture(), origin: { x: x, y: y } },
      { buffer: readback, bytesPerRow: stride, rowsPerImage: height },
      { width: width, height: height },
    );
    this.queue.submit([encoder.finish()]);
    const mapped = new Uint8Array(readback.getMappedRange());
    const pixels = new Uint8Array(rowBytes * height);
    for (let row = 0; row < height; row += 1) {
      const source = row * stride;
      const target = row * rowBytes;
      for (let column = 0; column < rowBytes; column += 1) {
        pixels[target + column] = mapped[source + column];
      }
    }
    readback.unmap();
    return pixels;
  }

  /** `y` is measured up from the bottom, matching the WebGL readPixels probe. */
  probe(x, y) {
    let px = x < 0 ? 0 : x;
    let py = y < 0 ? 0 : y;
    if (px >= this.width) px = this.width - 1;
    if (py >= this.height) py = this.height - 1;
    const pixels = this.readRegion(px, this.height - 1 - py, 1, 1);
    if (pixels === null || pixels.length < 4) return "0,0,0,0";
    const r = this.bgra ? pixels[2] : pixels[0];
    const b = this.bgra ? pixels[0] : pixels[2];
    return r + "," + pixels[1] + "," + b + "," + pixels[3];
  }

  /**
   * The brightest pixel near the centre of `x,y,width,height`. WebGPU reads a
   * 32×32 patch instead of the whole rectangle: the host report only needs a
   * representative bright sample, and the smaller copy avoids a large readback.
   */
  probeMax(x, y, width, height) {
    let size = 32;
    if (size > this.width) size = this.width;
    if (size > this.height) size = this.height;
    const centerX = x + Math.floor(width / 2);
    const centerY = y + Math.floor(height / 2);
    let sx = centerX - Math.floor(size / 2);
    let sy = this.height - centerY - Math.floor(size / 2);
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx > this.width - size) sx = this.width - size;
    if (sy > this.height - size) sy = this.height - size;
    const pixels = this.readRegion(sx, sy, size, size);
    if (pixels === null) return "0,0,0,0";
    const rowBytes = size * 4;
    let best = 0;
    let bestIndex = 0;
    for (let row = 0; row < size; row += 1) {
      for (let column = 0; column < size; column += 1) {
        const index = row * rowBytes + column * 4;
        const sum = pixels[index] + pixels[index + 1] + pixels[index + 2];
        if (sum > best) {
          best = sum;
          bestIndex = index;
        }
      }
    }
    const r = this.bgra ? pixels[bestIndex + 2] : pixels[bestIndex];
    const b = this.bgra ? pixels[bestIndex] : pixels[bestIndex + 2];
    return r + "," + pixels[bestIndex + 1] + "," + b + ",255";
  }

  snapshot() {
    /* The host report samples individual pixels; a full-canvas readback would
     * allocate 3 MB per call and is not used by the example. */
    return new Uint8Array(0);
  }
}
