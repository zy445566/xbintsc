/**
 * End-to-end test for resolving a half-float multisampled render target.
 *
 * `blitFramebuffer` of an RGBA16F multisample target is the resolve a three.js
 * `WebGLRenderTarget({ samples, type: HalfFloatType })` performs. SDL_GPU's
 * `SDL_BlitGPUTexture` is `vkCmdBlitImage`, and a half-float image does not
 * advertise BLIT_SRC/BLIT_DST on Vulkan: the blit silently produced black (and,
 * in a real page, eventually lost the device). The engine copies float images
 * with a sampling pass instead, so the resolved texel must carry the drawn
 * colour.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL 2 float MSAA resolve", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("copies an RGBA16F multisample resolve into the sampled texture", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-msaa-float-resolve",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "msaa-float", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2", { antialias: false });
        if (gl == null) {
          label("context", "missing");
          win.close();
          return;
        }
        label("max-samples", gl.getParameter(gl.MAX_SAMPLES));
        if (gl.getParameter(gl.MAX_SAMPLES) < 2) {
          label("skip", "no-msaa");
          win.close();
          return;
        }
        const size = 64;
        const colorRb: any = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, colorRb);
        gl.renderbufferStorageMultisample(gl.RENDERBUFFER, 4, gl.RGBA16F, size, size);
        const depthRb: any = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, depthRb);
        gl.renderbufferStorageMultisample(gl.RENDERBUFFER, 4, gl.DEPTH24_STENCIL8, size, size);
        const msaa: any = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, msaa);
        gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, colorRb);
        gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_STENCIL_ATTACHMENT, gl.RENDERBUFFER,
          depthRb);
        label("msaa-complete", gl.checkFramebufferStatus(gl.FRAMEBUFFER) ===
          gl.FRAMEBUFFER_COMPLETE);

        const colorTex: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, colorTex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, size, size, 0, gl.RGBA, gl.HALF_FLOAT, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        const depthTex: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, depthTex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.DEPTH_COMPONENT24, size, size, 0, gl.DEPTH_COMPONENT,
          gl.UNSIGNED_INT, null);
        const resolve: any = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, resolve);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, colorTex, 0);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, depthTex, 0);
        label("resolve-complete", gl.checkFramebufferStatus(gl.FRAMEBUFFER) ===
          gl.FRAMEBUFFER_COMPLETE);

        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\nin vec2 a_pos; void main(){ gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\nprecision highp float; out vec4 o; void main(){ o = vec4(0.25, 0.5, 0.75, 1.0); }");
        gl.compileShader(fs);
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        gl.useProgram(program);
        const buffer: any = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
        const loc: any = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(loc);
        gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);

        gl.bindFramebuffer(gl.FRAMEBUFFER, msaa);
        gl.viewport(0, 0, size, size);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("draw-error", gl.getError());

        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, msaa);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, resolve);
        gl.blitFramebuffer(0, 0, size, size, 0, 0, size, size,
          gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT, gl.NEAREST);
        label("blit-error", gl.getError());

        /* readPixels only reads RGBA8 targets, so sample the resolved
         * half-float texture while drawing the canvas and read that back. */
        const sampleVs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(sampleVs, "#version 300 es\\nin vec2 a_pos; out vec2 v_uv;" +
          "void main(){ v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(sampleVs);
        const sampleFs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(sampleFs, "#version 300 es\\nprecision highp float; in vec2 v_uv;" +
          "uniform sampler2D u_tex; out vec4 o; void main(){ o = texture(u_tex, v_uv); }");
        gl.compileShader(sampleFs);
        const sample: any = gl.createProgram();
        gl.attachShader(sample, sampleVs);
        gl.attachShader(sample, sampleFs);
        gl.linkProgram(sample);
        gl.useProgram(sample);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, colorTex);
        gl.uniform1i(gl.getUniformLocation(sample, "u_tex"), 0);
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        const sampleLoc: any = gl.getAttribLocation(sample, "a_pos");
        gl.enableVertexAttribArray(sampleLoc);
        gl.vertexAttribPointer(sampleLoc, 2, gl.FLOAT, false, 0, 0);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, size, size);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("sample-error", gl.getError());

        const pixel = new Uint8Array(4);
        gl.readPixels(size >> 1, size >> 1, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
        label("pixel", pixel[0] + "," + pixel[1] + "," + pixel[2] + "," + pixel[3]);
        win.close();
      });
      win.loadHTML(page);
      `,
      { console: true, autocloseMs: 5000, files: { "page.html": PAGE } },
    );

    const samples = Number(value("max-samples") ?? "0");
    if (samples < 2) return;
    expect(value("msaa-complete")).toBe("true");
    expect(value("resolve-complete")).toBe("true");
    expect(value("draw-error")).toBe("0");
    expect(value("blit-error")).toBe("0");
    expect(value("sample-error")).toBe("0");
    /* 0.25 * 255 = 64, 0.5 * 255 = 128, 0.75 * 255 = 191, with a little
     * rounding slack from the half-float round trip. */
    const pixel = (value("pixel") ?? "0,0,0,0").split(",").map(Number);
    expect(pixel[0]).toBeGreaterThan(40);
    expect(pixel[1]).toBeGreaterThan(90);
    expect(pixel[2]).toBeGreaterThan(140);
    expect(stderr).not.toContain("DEVICE_LOST");
  });
});
