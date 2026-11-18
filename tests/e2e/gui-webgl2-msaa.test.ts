/**
 * End-to-end tests for WebGL 2 multisample antialiasing.
 *
 * The engine resolves a multisampled drawing buffer at the end of every render
 * pass into the single-sampled texture the page composites, and supports
 * multisampled renderbuffers/textures that `blitFramebuffer` resolves. SDL_GPU's
 * Metal resolve is unreliable, so the backend only enables MSAA on Vulkan;
 * every assertion below is conditional on `MAX_SAMPLES`, which `win.driver()`
 * makes observable.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>msaa</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
  <canvas id="d" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL 2 MSAA", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("antialiases the default drawing buffer", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-msaa-default",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "msaa-default", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);
      const rgba = (p: any): string => p[0] + "," + p[1] + "," + p[2] + "," + p[3];

      const programFor = (gl: any): any => {
        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\nin vec2 a_pos;" +
          "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\nprecision highp float;" +
          "uniform vec4 u_color; out vec4 fragColor;" +
          "void main() { fragColor = u_color; }");
        gl.compileShader(fs);
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        return program;
      };

      win.on("ready", () => {
        const aa: any = document.getElementById("c");
        const one: any = document.getElementById("d");
        const gl: any = aa.getContext("webgl2");
        const gl1: any = one.getContext("webgl2", { antialias: false });
        label("driver", win.driver());
        if (gl == null || gl1 == null) {
          label("contexts", "missing");
          win.close();
          return;
        }
        label("max-samples", gl.getParameter(gl.MAX_SAMPLES));
        label("aa-attrs", gl.getContextAttributes().antialias + ":" +
          gl.getParameter(gl.SAMPLES) + ":" + gl.getParameter(gl.SAMPLE_BUFFERS));
        label("one-attrs", gl1.getContextAttributes().antialias + ":" +
          gl1.getParameter(gl1.SAMPLES) + ":" + gl1.getParameter(gl1.SAMPLE_BUFFERS));

        const draw = (context: any): void => {
          context.viewport(0, 0, 64, 64);
          context.clearColor(0, 0, 1, 1);
          context.clear(context.COLOR_BUFFER_BIT);
          const program = programFor(context);
          label("link-" + (context === gl ? "aa" : "one"),
            context.getProgramParameter(program, context.LINK_STATUS) === true);
          context.useProgram(program);
          const quad = context.createBuffer();
          context.bindBuffer(context.ARRAY_BUFFER, quad);
          context.bufferData(context.ARRAY_BUFFER, new Float32Array([
            -1, -0.9, 1, -0.9, 1, 0.9,
          ]), context.STATIC_DRAW);
          const pos: any = context.getAttribLocation(program, "a_pos");
          context.enableVertexAttribArray(pos);
          context.vertexAttribPointer(pos, 2, context.FLOAT, false, 0, 0);
          context.uniform4f(context.getUniformLocation(program, "u_color"), 1, 0, 0, 1);
          context.drawArrays(context.TRIANGLES, 0, 3);
        };

        const px = new Uint8Array(4);
        const probe = (context: any, x: number, y: number): string => {
          context.readPixels(x, y, 1, 1, context.RGBA, context.UNSIGNED_BYTE, px);
          return rgba(px);
        };
        draw(gl);
        draw(gl1);
        // The bottom edge sits at window y = 3.2, so the pixel row at GL y = 3
        // is 80% covered: MSAA resolves it to a red/blue mix, while the
        // single-sampled context snaps it to a fully covered pixel.
        label("aa-edge", probe(gl, 32, 3));
        label("aa-inside", probe(gl, 48, 20));
        label("aa-background", probe(gl, 16, 50));
        label("one-edge", probe(gl1, 32, 3));
        label("one-inside", probe(gl1, 48, 20));
        label("error", gl.getError() + ":" + gl1.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(["vulkan", "metal", "direct3d12"]).toContain(value("driver"));
    const maxSamples = Number(value("max-samples"));
    if (value("driver") === "vulkan") {
      expect(maxSamples).toBe(4);
      expect(value("aa-attrs")).toBe("true:4:1");
      expect(value("one-attrs")).toBe("false:0:0");
      expect(value("link-aa")).toBe("true");
      expect(value("link-one")).toBe("true");
      // A partially covered edge pixel is a blend; the fully covered and the
      // empty pixels keep the exact shader/clear colours.
      const edge = value("aa-edge").split(",").map(Number);
      expect(edge[0]).toBeGreaterThan(120);
      expect(edge[0]).toBeLessThan(240);
      expect(edge[2]).toBeGreaterThan(15);
      expect(edge[2]).toBeLessThan(130);
      expect(value("aa-inside")).toBe("255,0,0,255");
      expect(value("aa-background")).toBe("0,0,255,255");
      expect(value("one-edge")).toBe("255,0,0,255");
      expect(value("one-inside")).toBe("255,0,0,255");
    } else {
      // The backend keeps the single-sampled path; the attribute is honest.
      expect(maxSamples).toBe(0);
      expect(value("aa-attrs")).toBe("false:0:0");
      expect(value("one-attrs")).toBe("false:0:0");
      expect(value("aa-edge")).toBe("255,0,0,255");
      expect(value("aa-inside")).toBe("255,0,0,255");
      expect(value("aa-background")).toBe("0,0,255,255");
    }
    expect(value("error")).toBe("0:0");
    expect(stderr).not.toContain("pipeline creation failed");
  });

  it("renders into and resolves a multisampled renderbuffer", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-msaa-renderbuffer",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "msaa-rb", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);
      const rgba = (p: any): string => p[0] + "," + p[1] + "," + p[2] + "," + p[3];

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          label("context", "missing");
          win.close();
          return;
        }
        label("driver", win.driver());
        label("max-samples", gl.getParameter(gl.MAX_SAMPLES));
        const supported = gl.getParameter(gl.MAX_SAMPLES) > 0;
        if (!supported) {
          label("msaa", "unsupported");
          win.close();
          return;
        }
        const formats = gl.getInternalformatParameter(gl.RENDERBUFFER, gl.RGBA8, gl.SAMPLES);
        label("internalformat-samples", formats.length + ":" + formats[0]);

        const color = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, color);
        gl.renderbufferStorageMultisample(gl.RENDERBUFFER, 4, gl.RGBA8, 32, 32);
        label("rb", gl.getRenderbufferParameter(gl.RENDERBUFFER, gl.RENDERBUFFER_WIDTH) + "x" +
          gl.getRenderbufferParameter(gl.RENDERBUFFER, gl.RENDERBUFFER_HEIGHT) + ":" +
          gl.getRenderbufferParameter(gl.RENDERBUFFER, gl.RENDERBUFFER_SAMPLES) + ":" +
          gl.getRenderbufferParameter(gl.RENDERBUFFER, gl.RENDERBUFFER_INTERNAL_FORMAT));
        const depth = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, depth);
        gl.renderbufferStorageMultisample(gl.RENDERBUFFER, 4, gl.DEPTH_COMPONENT24, 32, 32);
        const fbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, color);
        gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depth);
        label("complete", gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);

        // Draw a green full-screen triangle through a 4x pipeline.
        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\nin vec2 a_pos;" +
          "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\nprecision highp float;" +
          "out vec4 fragColor; void main() { fragColor = vec4(0.0, 1.0, 0.0, 1.0); }");
        gl.compileShader(fs);
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        label("link", gl.getProgramParameter(program, gl.LINK_STATUS) === true);
        gl.useProgram(program);
        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
        const pos: any = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 0, 0);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(1, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("draw-error", gl.getError());

        // Resolve into a single-sampled texture and read it back.
        const resolve = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, resolve);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 32, 32, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        const target = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, target);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, resolve, 0);
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, fbo);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, target);
        gl.blitFramebuffer(0, 0, 32, 32, 0, 0, 32, 32, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        label("blit-error", gl.getError());
        gl.bindFramebuffer(gl.FRAMEBUFFER, target);
        const px = new Uint8Array(4);
        gl.readPixels(16, 16, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        label("resolved", rgba(px));

        // A multisampled *texture* follows the same path.
        const msaa = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, msaa);
        gl.texStorage2DMultisample(gl.TEXTURE_2D, 4, gl.RGBA8, 16, 16, true);
        label("texture-storage-error", gl.getError());
        const msaaFbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, msaaFbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, msaa, 0);
        label("texture-complete",
          gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
        gl.viewport(0, 0, 16, 16);
        gl.clearColor(1, 0, 1, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        const resolveSmall = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, resolveSmall);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 16, 16, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        const smallTarget = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, smallTarget);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D,
          resolveSmall, 0);
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, msaaFbo);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, smallTarget);
        gl.blitFramebuffer(0, 0, 16, 16, 0, 0, 16, 16, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        label("texture-blit-error", gl.getError());
        gl.bindFramebuffer(gl.FRAMEBUFFER, smallTarget);
        gl.readPixels(8, 8, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        label("texture-resolved", rgba(px));
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 15000 },
    );

    if (value("driver") === "vulkan") {
      expect(value("max-samples")).toBe("4");
      expect(value("internalformat-samples")).toBe("1:4");
      expect(value("rb")).toBe("32x32:4:" + String(0x8058));
      expect(value("complete")).toBe("true");
      expect(value("link")).toBe("true");
      expect(value("draw-error")).toBe("0");
      expect(value("blit-error")).toBe("0");
      expect(value("resolved")).toBe("0,255,0,255");
      expect(value("texture-storage-error")).toBe("0");
      expect(value("texture-complete")).toBe("true");
      expect(value("texture-blit-error")).toBe("0");
      expect(value("texture-resolved")).toBe("255,0,255,255");
      expect(value("error")).toBe("0");
    } else {
      expect(value("msaa")).toBe("unsupported");
    }
    expect(stderr).not.toContain("pipeline creation failed");
  });

  it("reports incomplete multisample framebuffers and rejects readPixels", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-msaa-status",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "msaa-status", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          label("context", "missing");
          win.close();
          return;
        }
        label("driver", win.driver());
        if (gl.getParameter(gl.MAX_SAMPLES) === 0) {
          label("msaa", "unsupported");
          win.close();
          return;
        }
        const color = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, color);
        gl.renderbufferStorageMultisample(gl.RENDERBUFFER, 4, gl.RGBA8, 16, 16);
        const singleDepth = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, singleDepth);
        gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, 16, 16);
        const fbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, color);
        gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER,
          singleDepth);
        label("mixed", gl.checkFramebufferStatus(gl.FRAMEBUFFER) ===
          gl.FRAMEBUFFER_INCOMPLETE_MULTISAMPLE);

        const multisampleDepth = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, multisampleDepth);
        gl.renderbufferStorageMultisample(gl.RENDERBUFFER, 4, gl.DEPTH_COMPONENT24, 16, 16);
        gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER,
          multisampleDepth);
        label("matched", gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
        const px = new Uint8Array(4);
        gl.readPixels(8, 8, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        label("read-error", gl.getError() === gl.INVALID_OPERATION);
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    if (value("driver") === "vulkan") {
      expect(value("mixed")).toBe("true");
      expect(value("matched")).toBe("true");
      expect(value("read-error")).toBe("true");
      expect(value("error")).toBe("0");
    } else {
      expect(value("msaa")).toBe("unsupported");
    }
    expect(stderr).not.toContain("pipeline creation failed");
  });
});
