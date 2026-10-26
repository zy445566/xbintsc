/**
 * End-to-end test for WebGL 2 multiple render targets.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl2-mrt</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL 2 MRT", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("writes two fragment outputs and reads each attachment", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-mrt",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "mrt", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }
        label("max-draw", gl.getParameter(gl.MAX_DRAW_BUFFERS));

        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\n" +
          "precision highp float;" +
          "out vec4 fragColor0; out vec4 fragColor1;" +
          "void main() { fragColor0 = vec4(1.0, 0.0, 0.0, 1.0);" +
          "  fragColor1 = vec4(0.0, 1.0, 0.0, 1.0); }");
        gl.compileShader(fs);
        label("fs", gl.getShaderParameter(fs, gl.COMPILE_STATUS) === true);
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        label("link", gl.getProgramParameter(program, gl.LINK_STATUS) === true);
        gl.useProgram(program);

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1,  3, -1,  -1, 3,
        ]), gl.STATIC_DRAW);
        const pos: any = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);

        const make_target = (): any => {
          const texture: any = gl.createTexture();
          gl.bindTexture(gl.TEXTURE_2D, texture);
          gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 32, 32, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
          return texture;
        };
        const first: any = make_target();
        const second: any = make_target();
        const fbo: any = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, first, 0);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, gl.TEXTURE_2D, second, 0);
        gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1]);
        label("status", gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
        label("draw-buffer-1", gl.getParameter(gl.DRAW_BUFFER1) === gl.COLOR_ATTACHMENT1);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(0, 0, 0, 0);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        const px = new Uint8Array(4);
        const read = (attachment: number): string => {
          gl.readBuffer(attachment);
          gl.readPixels(16, 16, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };
        label("target-0", read(gl.COLOR_ATTACHMENT0));
        label("target-1", read(gl.COLOR_ATTACHMENT1));
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(value("max-draw")).toBe("4");
    expect(value("fs")).toBe("true");
    expect(value("link")).toBe("true");
    expect(value("status")).toBe("true");
    expect(value("draw-buffer-1")).toBe("true");
    expect(value("target-0")).toBe("255,0,0,255");
    expect(value("target-1")).toBe("0,255,0,255");
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });
});
