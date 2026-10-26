/**
 * End-to-end tests for the WebGL 2 format and framebuffer additions:
 * stencil, float/depth textures, framebuffer blits, scissored clears,
 * `ImageBitmap` uploads and compressed textures.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl2-formats</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL 2 formats", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("supports stencil operations, float textures and depth textures", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-formats",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "formats", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }
        const px = new Uint8Array(4);
        const probe = (): string => {
          gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };
        label("stencil-bits", gl.getParameter(gl.STENCIL_BITS));
        label("stencil-attr", gl.getContextAttributes().stencil);

        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; uniform float u_z;" +
          "void main() { gl_Position = vec4(a_pos, u_z, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\n" +
          "precision highp float; uniform vec4 u_color; out vec4 fragColor;" +
          "void main() { fragColor = u_color; }");
        gl.compileShader(fs);
        const flat: any = gl.createProgram();
        gl.attachShader(flat, vs);
        gl.attachShader(flat, fs);
        gl.linkProgram(flat);
        label("flat-link", gl.getProgramParameter(flat, gl.LINK_STATUS) === true);

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1,  3, -1,  -1, 3,
        ]), gl.STATIC_DRAW);
        gl.useProgram(flat);
        const pos: any = gl.getAttribLocation(flat, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        gl.uniform1f(gl.getUniformLocation(flat, "u_z"), 0.0);
        const flat_color: any = gl.getUniformLocation(flat, "u_color");

        // --- stencil -------------------------------------------------------
        gl.viewport(0, 0, 64, 64);
        gl.clearColor(0, 0, 0, 1);
        gl.clearStencil(0);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT);
        gl.enable(gl.STENCIL_TEST);
        gl.stencilMask(255);
        gl.stencilFunc(gl.ALWAYS, 1, 255);
        gl.stencilOp(gl.KEEP, gl.KEEP, gl.REPLACE);
        gl.uniform4f(flat_color, 1, 1, 1, 1);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        gl.stencilFunc(gl.EQUAL, 1, 255);
        gl.stencilOp(gl.KEEP, gl.KEEP, gl.KEEP);
        gl.uniform4f(flat_color, 1, 0, 0, 1);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("stencil-red", probe());
        gl.stencilFunc(gl.EQUAL, 0, 255);
        gl.uniform4f(flat_color, 0, 1, 0, 1);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("stencil-blocked", probe());
        gl.clear(gl.COLOR_BUFFER_BIT | gl.STENCIL_BUFFER_BIT);
        gl.stencilFunc(gl.EQUAL, 1, 255);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("stencil-cleared", probe());
        gl.disable(gl.STENCIL_TEST);

        // --- float texture sampling ---------------------------------------
        const sample_vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(sample_vs, "#version 300 es\\n" +
          "in vec2 a_pos; out vec2 v_uv;" +
          "void main() { v_uv = a_pos * 0.5 + 0.5;" +
          "  gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(sample_vs);
        const sample_fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(sample_fs, "#version 300 es\\n" +
          "precision highp float; in vec2 v_uv; uniform sampler2D u_tex;" +
          "out vec4 fragColor;" +
          "void main() { fragColor = texture(u_tex, v_uv); }");
        gl.compileShader(sample_fs);
        const sample: any = gl.createProgram();
        gl.attachShader(sample, sample_vs);
        gl.attachShader(sample, sample_fs);
        gl.linkProgram(sample);
        label("sample-link", gl.getProgramParameter(sample, gl.LINK_STATUS) === true);

        const float_tex = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, float_tex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, 2, 2, 0, gl.RGBA, gl.FLOAT,
          new Float32Array([0.25, 0.5, 0.75, 1,  0.25, 0.5, 0.75, 1,
                            0.25, 0.5, 0.75, 1,  0.25, 0.5, 0.75, 1]));
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.useProgram(sample);
        gl.uniform1i(gl.getUniformLocation(sample, "u_tex"), 0);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("float-texture", probe());

        // --- render into an RGBA16F target and sample it -------------------
        const target = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, target);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, 32, 32, 0, gl.RGBA, gl.FLOAT, null);
        const fbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, target, 0);
        label("float-fbo", gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(0.2, 0.4, 0.6, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, 64, 64);
        gl.bindTexture(gl.TEXTURE_2D, target);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("float-target", probe());

        // --- depth texture as a framebuffer depth attachment ---------------
        const depth_tex = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, depth_tex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.DEPTH_COMPONENT24, 32, 32, 0,
          gl.DEPTH_COMPONENT, gl.UNSIGNED_INT, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        const depth_color = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, depth_color);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 32, 32, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        const depth_fbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, depth_fbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, depth_color, 0);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, depth_tex, 0);
        label("depth-fbo", gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(0, 0, 0, 1);
        gl.clearDepth(1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LESS);
        gl.useProgram(flat);
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.enableVertexAttribArray(gl.getAttribLocation(flat, "a_pos"));
        gl.vertexAttribPointer(gl.getAttribLocation(flat, "a_pos"), 2, gl.FLOAT, false, 8, 0);
        gl.uniform1f(gl.getUniformLocation(flat, "u_z"), 0.5);
        gl.uniform4f(flat_color, 1, 1, 1, 1);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        gl.disable(gl.DEPTH_TEST);

        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, 64, 64);
        const depth_fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(depth_fs, "#version 300 es\\n" +
          "precision highp float; in vec2 v_uv; uniform sampler2D u_tex;" +
          "out vec4 fragColor;" +
          "void main() { fragColor = vec4(vec3(texture(u_tex, v_uv).r), 1.0); }");
        gl.compileShader(depth_fs);
        label("depth-fs", gl.getShaderParameter(depth_fs, gl.COMPILE_STATUS) === true);
        const depth_program: any = gl.createProgram();
        gl.attachShader(depth_program, sample_vs);
        gl.attachShader(depth_program, depth_fs);
        gl.linkProgram(depth_program);
        label("depth-link", gl.getProgramParameter(depth_program, gl.LINK_STATUS) === true);
        gl.useProgram(depth_program);
        gl.uniform1i(gl.getUniformLocation(depth_program, "u_tex"), 0);
        gl.bindTexture(gl.TEXTURE_2D, depth_tex);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("depth-pixel", probe());
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    const near = (actual: string, expected: number[], tolerance: number): void => {
      const parts = actual.split(",").map((part) => Number(part));
      expect(parts.length).toBe(expected.length);
      for (let i = 0; i < expected.length; i++) {
        const actual = parts[i] ?? Number.POSITIVE_INFINITY;
        const want = expected[i] ?? Number.POSITIVE_INFINITY;
        expect(Math.abs(actual - want)).toBeLessThanOrEqual(tolerance);
      }
    };

    expect(value("stencil-bits")).toBe("8");
    expect(value("stencil-attr")).toBe("true");
    expect(value("flat-link")).toBe("true");
    expect(value("stencil-red")).toBe("255,0,0,255");
    expect(value("stencil-blocked")).toBe("255,0,0,255");
    expect(value("stencil-cleared")).toBe("0,0,0,255");
    expect(value("sample-link")).toBe("true");
    near(value("float-texture"), [64, 128, 191, 255], 2);
    expect(value("float-fbo")).toBe("true");
    near(value("float-target"), [51, 102, 153, 255], 3);
    expect(value("depth-fbo")).toBe("true");
    expect(value("depth-fs")).toBe("true");
    expect(value("depth-link")).toBe("true");
    // The engine hands GLSL to Vulkan/Metal directly, so the stored depth is
    // the backend's clip-space z (0.5), not GL's remapped (z + 1) / 2 (0.75).
    near(value("depth-pixel"), [128, 128, 128, 255], 4);
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });

  it("blits framebuffers and clears a scissor rectangle", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-blit",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "blit", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }
        const px = new Uint8Array(4);
        const probe = (x: number, y: number): string => {
          gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };
        const red = (x: number, y: number): string => {
          gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return "" + px[0];
        };

        const make_program = (vs_source: string, fs_source: string): any => {
          const vs: any = gl.createShader(gl.VERTEX_SHADER);
          gl.shaderSource(vs, vs_source);
          gl.compileShader(vs);
          const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
          gl.shaderSource(fs, fs_source);
          gl.compileShader(fs);
          const program: any = gl.createProgram();
          gl.attachShader(program, vs);
          gl.attachShader(program, fs);
          gl.linkProgram(program);
          return program;
        };
        const flat = make_program(
          "#version 300 es\\nin vec2 a_pos; uniform float u_z;" +
          "void main() { gl_Position = vec4(a_pos, u_z, 1.0); }",
          "#version 300 es\\nprecision highp float; uniform vec4 u_color;" +
          "out vec4 fragColor; void main() { fragColor = u_color; }");
        const sampler = make_program(
          "#version 300 es\\nin vec2 a_pos; out vec2 v_uv;" +
          "void main() { v_uv = a_pos * 0.5 + 0.5;" +
          "  gl_Position = vec4(a_pos, 0.0, 1.0); }",
          "#version 300 es\\nprecision highp float; in vec2 v_uv;" +
          "uniform sampler2D u_tex; out vec4 fragColor;" +
          "void main() { fragColor = vec4(vec3(texture(u_tex, v_uv).r), 1.0); }");
        label("programs", gl.getProgramParameter(flat, gl.LINK_STATUS) === true &&
          gl.getProgramParameter(sampler, gl.LINK_STATUS) === true);

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1,  3, -1,  -1, 3,
        ]), gl.STATIC_DRAW);
        const draw_flat = (r: number, g: number, b: number, z: number): void => {
          gl.useProgram(flat);
          gl.bindBuffer(gl.ARRAY_BUFFER, quad);
          gl.enableVertexAttribArray(gl.getAttribLocation(flat, "a_pos"));
          gl.vertexAttribPointer(gl.getAttribLocation(flat, "a_pos"), 2, gl.FLOAT, false, 8, 0);
          gl.uniform1f(gl.getUniformLocation(flat, "u_z"), z);
          gl.uniform4f(gl.getUniformLocation(flat, "u_color"), r, g, b, 1);
          gl.drawArrays(gl.TRIANGLES, 0, 3);
        };
        const show_depth = (depth: any): string => {
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.viewport(0, 0, 64, 64);
          gl.useProgram(sampler);
          gl.uniform1i(gl.getUniformLocation(sampler, "u_tex"), 0);
          gl.bindTexture(gl.TEXTURE_2D, depth);
          gl.clearColor(0, 0, 0, 1);
          gl.clear(gl.COLOR_BUFFER_BIT);
          gl.drawArrays(gl.TRIANGLES, 0, 3);
          return red(8, 32) + "," + red(56, 32) + "," + red(32, 8) + "," + red(32, 56);
        };

        // --- scissored colour clear ---------------------------------------
        gl.viewport(0, 0, 64, 64);
        gl.clearColor(1, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.enable(gl.SCISSOR_TEST);
        gl.scissor(8, 8, 16, 16);
        gl.clearColor(0, 0, 1, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.disable(gl.SCISSOR_TEST);
        label("scissor-inside", probe(16, 16));
        label("scissor-outside", probe(2, 2));

        // --- render to a texture and blit it back -------------------------
        const target = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, target);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 32, 32, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        const fbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, target, 0);
        label("fbo", gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(0, 1, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        draw_flat(0, 0, 1, 0);
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, fbo);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        gl.blitFramebuffer(0, 0, 32, 32, 0, 0, 64, 64, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        label("blit", probe(32, 32));

        // --- scissored depth clear ---------------------------------------
        const depth = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, depth);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.DEPTH_COMPONENT24, 32, 32, 0,
          gl.DEPTH_COMPONENT, gl.UNSIGNED_INT, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        const depth_color = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, depth_color);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 32, 32, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        const depth_fbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, depth_fbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, depth_color, 0);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, depth, 0);
        gl.viewport(0, 0, 32, 32);
        gl.clearDepth(1);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LESS);
        draw_flat(1, 1, 1, 0.5);
        gl.disable(gl.DEPTH_TEST);
        label("depth-initial", show_depth(depth));

        gl.bindFramebuffer(gl.FRAMEBUFFER, depth_fbo);
        gl.viewport(0, 0, 32, 32);
        gl.enable(gl.SCISSOR_TEST);
        gl.scissor(0, 0, 16, 32);
        gl.clearDepth(0);
        gl.clear(gl.DEPTH_BUFFER_BIT);
        gl.disable(gl.SCISSOR_TEST);
        label("depth-half", show_depth(depth));

        gl.bindFramebuffer(gl.FRAMEBUFFER, depth_fbo);
        gl.enable(gl.SCISSOR_TEST);
        gl.scissor(0, 0, 32, 32);
        gl.clearDepth(1);
        gl.clear(gl.DEPTH_BUFFER_BIT);
        gl.disable(gl.SCISSOR_TEST);
        label("depth-full", show_depth(depth));
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 12000 },
    );

    expect(value("programs")).toBe("true");
    expect(value("scissor-inside")).toBe("0,0,255,255");
    expect(value("scissor-outside")).toBe("255,0,0,255");
    expect(value("fbo")).toBe("true");
    expect(value("blit")).toBe("0,0,255,255");
    expect(value("depth-initial")).toBe("128,128,128,128");
    expect(value("depth-half")).toBe("0,128,128,128");
    expect(value("depth-full")).toBe("255,255,255,255");
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });

  it("uploads an ImageBitmap created from an ImageData-shaped source", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-bitmap",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "bitmap", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", async () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }
        const bitmap: any = await createImageBitmap({
          width: 2,
          height: 2,
          data: new Uint8Array([64, 128, 191, 255,  64, 128, 191, 255,
                                64, 128, 191, 255,  64, 128, 191, 255]),
        });
        label("bitmap-size", bitmap.width + "x" + bitmap.height);
        label("bitmap-type", typeof ImageBitmap === "function");

        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; out vec2 v_uv;" +
          "void main() { v_uv = a_pos * 0.5 + 0.5;" +
          "  gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\n" +
          "precision highp float; in vec2 v_uv; uniform sampler2D u_tex;" +
          "out vec4 fragColor; void main() { fragColor = texture(u_tex, v_uv); }");
        gl.compileShader(fs);
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        label("link", gl.getProgramParameter(program, gl.LINK_STATUS) === true);
        gl.useProgram(program);

        const texture: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, bitmap);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.uniform1i(gl.getUniformLocation(program, "u_tex"), 0);
        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1,  3, -1,  -1, 3,
        ]), gl.STATIC_DRAW);
        const pos: any = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        gl.viewport(0, 0, 64, 64);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        const px = new Uint8Array(4);
        gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        label("pixel", px[0] + "," + px[1] + "," + px[2] + "," + px[3]);
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(value("bitmap-size")).toBe("2x2");
    expect(value("bitmap-type")).toBe("true");
    expect(value("link")).toBe("true");
    expect(value("pixel")).toBe("64,128,191,255");
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });

  it("samples an S3TC-compressed texture when the device supports it", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-compressed",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "compressed", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }
        const s3tc: any = gl.getExtension("WEBGL_compressed_texture_s3tc");
        label("s3tc", s3tc != null);
        const supported: any = gl.getSupportedExtensions();
        label("listed", supported.indexOf("WEBGL_compressed_texture_s3tc") >= 0);
        if (s3tc == null) {
          win.close();
          return;
        }
        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; out vec2 v_uv;" +
          "void main() { v_uv = a_pos * 0.5 + 0.5;" +
          "  gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\n" +
          "precision highp float; in vec2 v_uv; uniform sampler2D u_tex;" +
          "out vec4 fragColor; void main() { fragColor = texture(u_tex, v_uv); }");
        gl.compileShader(fs);
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        label("link", gl.getProgramParameter(program, gl.LINK_STATUS) === true);
        gl.useProgram(program);

        const texture: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        // One BC1 block: colour0 = red (RGB565 0xF800), colour1 = black,
        // every index selects colour0.
        gl.compressedTexImage2D(gl.TEXTURE_2D, 0, s3tc.COMPRESSED_RGBA_S3TC_DXT1_EXT,
          4, 4, 0, new Uint8Array([0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0]));
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.uniform1i(gl.getUniformLocation(program, "u_tex"), 0);
        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1,  3, -1,  -1, 3,
        ]), gl.STATIC_DRAW);
        const pos: any = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        gl.viewport(0, 0, 64, 64);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        const px = new Uint8Array(4);
        gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        label("pixel", px[0] + "," + px[1] + "," + px[2] + "," + px[3]);
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(["true", "false"]).toContain(value("s3tc"));
    expect(value("listed")).toBe(value("s3tc"));
    if (value("s3tc") === "true") {
      expect(value("link")).toBe("true");
      expect(value("pixel")).toBe("255,0,0,255");
      expect(value("error")).toBe("0");
    }
    expect(stderr).not.toContain("pipeline creation failed");
  });
});
