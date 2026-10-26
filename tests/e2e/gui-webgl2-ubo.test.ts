/**
 * End-to-end test for WebGL 2 uniform buffer objects.
 *
 * The engine merges a user block into its single std140 block and copies the
 * bound buffer into that block at draw time, because SDL_GPU has no bindable
 * uniform buffers.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl2-ubo</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL 2 UBO", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("feeds a uniform block from a bound buffer", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-ubo",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "ubo", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }
        const block = "layout(std140) uniform Camera {" +
          "  vec4 u_color; float u_scale; } cam;";
        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" + block +
          "in vec2 a_pos;" +
          "void main() { gl_Position = vec4(a_pos * cam.u_scale, 0.0, 1.0); }");
        gl.compileShader(vs);
        label("vs", gl.getShaderParameter(vs, gl.COMPILE_STATUS) === true);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\nprecision highp float;\\n" + block +
          "out vec4 fragColor;" +
          "void main() { fragColor = cam.u_color; }");
        gl.compileShader(fs);
        label("fs", gl.getShaderParameter(fs, gl.COMPILE_STATUS) === true);
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        label("link", gl.getProgramParameter(program, gl.LINK_STATUS) === true);
        gl.useProgram(program);

        const index: any = gl.getUniformBlockIndex(program, "Camera");
        label("block-index", index != 4294967295);
        label("block-name", gl.getActiveUniformBlockName(program, index));
        gl.uniformBlockBinding(program, index, 0);
        label("binding", gl.getActiveUniformBlockParameter(
          program, index, gl.UNIFORM_BLOCK_BINDING));
        label("size", gl.getActiveUniformBlockParameter(
          program, index, gl.UNIFORM_BLOCK_DATA_SIZE) >= 20);

        const data = new Float32Array([1, 0, 0, 1, 1]);
        const buffer: any = gl.createBuffer();
        gl.bindBuffer(gl.UNIFORM_BUFFER, buffer);
        gl.bufferData(gl.UNIFORM_BUFFER, data, gl.DYNAMIC_DRAW);
        gl.bindBufferBase(gl.UNIFORM_BUFFER, 0, buffer);
        label("buffer-bound", gl.getIndexedParameter(gl.UNIFORM_BUFFER, 0) != null);

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1,  3, -1,  -1, 3,
        ]), gl.STATIC_DRAW);
        const pos: any = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        gl.viewport(0, 0, 64, 64);
        const px = new Uint8Array(4);
        const sample = (): string => {
          gl.clearColor(0, 0, 1, 1);
          gl.clear(gl.COLOR_BUFFER_BIT);
          gl.drawArrays(gl.TRIANGLES, 0, 3);
          gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };
        label("red", sample());
        gl.bufferSubData(gl.UNIFORM_BUFFER, 0, new Float32Array([0, 1, 0, 1]));
        label("green", sample());
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(value("vs")).toBe("true");
    expect(value("fs")).toBe("true");
    expect(value("link")).toBe("true");
    expect(value("block-index")).toBe("true");
    expect(value("block-name")).toBe("Camera");
    expect(value("binding")).toBe("0");
    expect(value("size")).toBe("true");
    expect(value("buffer-bound")).toBe("true");
    expect(value("red")).toBe("255,0,0,255");
    expect(value("green")).toBe("0,255,0,255");
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });
});
