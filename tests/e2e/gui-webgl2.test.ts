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
});
