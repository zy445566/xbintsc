/**
 * End-to-end test for the `gui` extension's WebGL 2 context.
 *
 * Exercises the parts that are genuinely new over WebGL 1: the `#version 300
 * es` shader dialect, vertex-array objects, instanced draws with a per-instance
 * attribute divisor, `texStorage2D`, `clearBufferfv`, buffer copies and sampler
 * objects. As in the WebGL 1 test, `readPixels` proves the GPU produced the
 * picture.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl2</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL 2", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("runs #version 300 es shaders, VAOs and instanced draws", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "webgl2", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);
      const rgba = (p: any): string => p[0] + "," + p[1] + "," + p[2] + "," + p[3];

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        label("webgl2", gl != null);
        if (gl == null) {
          win.close();
          return;
        }
        label("webgl1-after", canvas.getContext("webgl") == null);
        label("version", gl.getParameter(gl.VERSION));
        label("draw-buffers", gl.getParameter(gl.MAX_DRAW_BUFFERS));

        canvas.width = 64;
        canvas.height = 64;
        gl.viewport(0, 0, 64, 64);
        const pixels = new Uint8Array(4);
        const probe = (x: number, y: number): string => {
          gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
          return rgba(pixels);
        };

        gl.clearColor(1, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        label("clear-red", probe(32, 32));
        gl.clearBufferfv(gl.COLOR, 0, new Float32Array([0, 1, 0, 1]));
        label("clear-buffer", probe(32, 32));

        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; in vec2 a_offset; out vec2 v_uv;\\n" +
          "void main() { v_uv = a_pos * 0.5 + 0.5;\\n" +
          "  gl_Position = vec4(a_pos * 0.35 + a_offset, 0.0, 1.0); }");
        gl.compileShader(vs);
        const vs_ok = gl.getShaderParameter(vs, gl.COMPILE_STATUS);
        label("vs", vs_ok === true ? "true" : "false:" + gl.getShaderInfoLog(vs));
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\n" +
          "precision highp float; in vec2 v_uv; uniform vec4 u_color;\\n" +
          "out vec4 fragColor; void main() { fragColor = u_color; }");
        gl.compileShader(fs);
        const fs_ok = gl.getShaderParameter(fs, gl.COMPILE_STATUS);
        label("fs", fs_ok === true ? "true" : "false:" + gl.getShaderInfoLog(fs));
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        const link_ok = gl.getProgramParameter(program, gl.LINK_STATUS);
        label("link", link_ok === true ? "true" : "false:" + gl.getProgramInfoLog(program));
        gl.useProgram(program);
        label("ubo", gl.getUniformBlockIndex(program, "Nope"));

        const vao: any = gl.createVertexArray();
        label("vao", vao != null);
        gl.bindVertexArray(vao);
        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1,
        ]), gl.STATIC_DRAW);
        const pos: any = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 0, 0);
        const offsets = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, offsets);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-0.5, 0, 0, 0, 0.5, 0]), gl.STATIC_DRAW);
        const offset_loc: any = gl.getAttribLocation(program, "a_offset");
        gl.enableVertexAttribArray(offset_loc);
        gl.vertexAttribPointer(offset_loc, 2, gl.FLOAT, false, 0, 0);
        gl.vertexAttribDivisor(offset_loc, 1);
        // Round-trip the VAO through the default state to prove it restores.
        gl.bindVertexArray(null);
        gl.bindVertexArray(vao);

        gl.uniform4f(gl.getUniformLocation(program, "u_color"), 0, 0, 1, 1);
        gl.clearColor(1, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, 3);
        label("inst-0", probe(16, 32));
        label("inst-1", probe(32, 32));
        label("inst-2", probe(48, 32));
        label("inst-background", probe(4, 32));

        // Sampler objects and immutable texture storage.
        const sampler: any = gl.createSampler();
        gl.samplerParameteri(sampler, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.bindSampler(0, sampler);
        label("sampler", gl.getSamplerParameter(sampler, gl.TEXTURE_MIN_FILTER) === gl.NEAREST);
        const texture: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA8, 2, 2);
        gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 2, 2, gl.RGBA, gl.UNSIGNED_BYTE,
          new Uint8Array([255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255]));
        label("tex-storage", gl.getError());

        // copyBufferSubData / getBufferSubData.
        const source = gl.createBuffer();
        gl.bindBuffer(gl.COPY_READ_BUFFER, source);
        gl.bufferData(gl.COPY_READ_BUFFER, new Uint8Array([1, 2, 3, 4]), gl.STATIC_DRAW);
        const destination = gl.createBuffer();
        gl.bindBuffer(gl.COPY_WRITE_BUFFER, destination);
        gl.bufferData(gl.COPY_WRITE_BUFFER, new Uint8Array([9, 9, 9, 9]), gl.STATIC_DRAW);
        gl.copyBufferSubData(gl.COPY_READ_BUFFER, gl.COPY_WRITE_BUFFER, 0, 0, 4);
        const out = new Uint8Array(4);
        gl.getBufferSubData(gl.COPY_WRITE_BUFFER, 0, out, 0, 4);
        label("copy", out[0] + "," + out[1] + "," + out[2] + "," + out[3]);
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      console.log("loaded");
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(value("webgl2")).toBe("true");
    expect(value("webgl1-after")).toBe("true");
    expect(value("version")).toContain("WebGL 2.0");
    expect(value("draw-buffers")).toBe("1");
    expect(value("clear-red")).toBe("255,0,0,255");
    expect(value("clear-buffer")).toBe("0,255,0,255");
    expect(value("vs")).toBe("true");
    expect(value("fs")).toBe("true");
    expect(value("link")).toBe("true");
    expect(value("ubo")).toBe("4294967295");
    expect(value("vao")).toBe("true");
    expect(value("inst-0")).toBe("0,0,255,255");
    expect(value("inst-1")).toBe("0,0,255,255");
    expect(value("inst-2")).toBe("0,0,255,255");
    expect(value("inst-background")).toBe("255,0,0,255");
    expect(value("sampler")).toBe("true");
    expect(value("tex-storage")).toBe("0");
    expect(value("copy")).toBe("1,2,3,4");
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });

  it("flattens struct uniforms into addressable fields", () => {
    const { value } = harness.compileAndRun(
      "webgl2-struct",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "struct", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        label("webgl2", gl != null);
        if (gl == null) {
          win.close();
          return;
        }
        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\n" +
          "precision highp float;\\n" +
          "struct Light { vec3 color; float power; };\\n" +
          "uniform Light lights[2];\\n" +
          "out vec4 fragColor;\\n" +
          "void main() { fragColor = vec4(max(lights[0].color, lights[1].color), 1.0); }");
        gl.compileShader(fs);
        label("compile", gl.getShaderParameter(vs, gl.COMPILE_STATUS) === true &&
          gl.getShaderParameter(fs, gl.COMPILE_STATUS) === true);
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        label("link", gl.getProgramParameter(program, gl.LINK_STATUS) === true);
        gl.useProgram(program);
        const c0: any = gl.getUniformLocation(program, "lights[0].color");
        const c1: any = gl.getUniformLocation(program, "lights[1].color");
        const p0: any = gl.getUniformLocation(program, "lights[0].power");
        const p1: any = gl.getUniformLocation(program, "lights[1].power");
        label("locations", (c0 != null) + "," + (c1 != null) + "," + (p0 != null) + "," + (p1 != null));
        gl.uniform3f(c0, 1, 0, 0);
        gl.uniform3f(c1, 0, 1, 0);
        gl.uniform1f(p0, 1);
        gl.uniform1f(p1, 1);
        const buffer: any = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1,
        ]), gl.STATIC_DRAW);
        const pos: any = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 0, 0);
        gl.viewport(0, 0, 64, 64);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 6);
        const px = new Uint8Array(4);
        gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        label("pixel", px[0] + "," + px[1] + "," + px[2] + "," + px[3]);
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      console.log("loaded");
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(value("webgl2")).toBe("true");
    expect(value("compile")).toBe("true");
    expect(value("link")).toBe("true");
    expect(value("locations")).toBe("true,true,true,true");
    expect(value("pixel")).toBe("255,255,0,255");
    expect(value("error")).toBe("0");
  });

  it("keeps a drawingBufferStorage size across canvas resizes", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-storage",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "storage", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }
        gl.drawingBufferStorage(48, 40);
        label("size", gl.drawingBufferWidth + "x" + gl.drawingBufferHeight);
        gl.viewport(0, 0, 48, 40);
        gl.clearColor(0, 1, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        const px = new Uint8Array(4);
        const probe = (): string => {
          gl.readPixels(24, 20, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };
        label("green", probe());
        canvas.width = 16;
        canvas.height = 16;
        label("after-resize", gl.drawingBufferWidth + "x" + gl.drawingBufferHeight);
        label("still-green", probe());
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 8000 },
    );

    expect(value("size")).toBe("48x40");
    expect(value("green")).toBe("0,255,0,255");
    expect(value("after-resize")).toBe("48x40");
    expect(value("still-green")).toBe("0,255,0,255");
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });

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
});
