/**
 * End-to-end test for two WebGL semantics that three.js's PMREMGenerator
 * depends on and that used to be wrong:
 *
 * - a fresh render target's depth attachment starts cleared to 1, so a
 *   depth-tested draw into a target the page never cleared must still pass;
 * - `uniform1fv` accepts a plain JavaScript array, not only a typed array
 *   (three.js hands the PMREM blur's `weights` over as a plain array).
 *
 * Without either one a three.js scene's environment map comes out black and
 * every surface outside direct sunlight renders black.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl-depth-init</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL depth init and array uniforms", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("clears a new depth attachment and uploads a plain-array uniform", () => {
    const { value } = harness.compileAndRun(
      "webgl-depth-init",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "depth-init", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2", { antialias: false });
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);

        const programFor = (frag: string, z: string, name: string): any => {
          const vs: any = gl.createShader(gl.VERTEX_SHADER);
          gl.shaderSource(vs,
            "#version 300 es\\n" +
            "in vec2 a_pos;\\n" +
            "void main() { gl_Position = vec4(a_pos, " + z + ", 1.0); }");
          gl.compileShader(vs);
          const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
          gl.shaderSource(fs, "#version 300 es\\nprecision highp float;\\n" + frag);
          gl.compileShader(fs);
          const program: any = gl.createProgram();
          gl.attachShader(program, vs);
          gl.attachShader(program, fs);
          gl.linkProgram(program);
          label(name + "-link", gl.getProgramParameter(program, gl.LINK_STATUS) === true);
          return program;
        };

        const attach = (program: any): void => {
          gl.useProgram(program);
          const pos: any = gl.getAttribLocation(program, "a_pos");
          gl.enableVertexAttribArray(pos);
          gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        };

        /* -- 1. depth-tested draw into a target that was never depth-cleared -- */
        const color = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, color);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 64, 64, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        const depth = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, depth);
        gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT16, 64, 64);
        const fbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, color, 0);
        gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depth);
        label("fbo", gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);

        const red = programFor(
          "out vec4 color; void main() { color = vec4(1.0, 0.0, 0.0, 1.0); }",
          "0.0", "red");
        const green = programFor(
          "out vec4 color; void main() { color = vec4(0.0, 1.0, 0.0, 1.0); }",
          "0.5", "green");

        gl.viewport(0, 0, 64, 64);
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LESS);
        gl.depthMask(true);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);   // deliberately no DEPTH_BUFFER_BIT
        attach(red);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        /* The farther green triangle must be rejected by the depth test; if the
         * depth buffer were disabled or uninitialised, green would win. */
        attach(green);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        const px = new Uint8Array(4);
        gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        label("depth", px[0] + "," + px[1] + "," + px[2] + "," + px[3]);

        /* -- 2. uniform1fv with a plain JavaScript array -- */
        const arrayProgram = programFor(
          "uniform float w[4]; out vec4 color; void main() {" +
          "  color = vec4(w[0], w[1], w[2], w[3]); }",
          "0.0", "array");
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.disable(gl.DEPTH_TEST);
        gl.viewport(0, 0, 64, 64);
        gl.clearColor(0, 0, 0, 0);
        gl.clear(gl.COLOR_BUFFER_BIT);
        attach(arrayProgram);
        gl.uniform1fv(gl.getUniformLocation(arrayProgram, "w"), [0.25, 0.5, 0.75, 1.0]);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        label("array", px[0] + "," + px[1] + "," + px[2] + "," + px[3]);
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(value("fbo")).toBe("true");
    expect(value("red-link")).toBe("true");
    expect(value("green-link")).toBe("true");
    expect(value("depth")).toBe("255,0,0,255");
    expect(value("array")).toBe("64,127,191,255");
    expect(value("error")).toBe("0");
  });
});
