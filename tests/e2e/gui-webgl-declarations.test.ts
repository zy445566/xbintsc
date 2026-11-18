/**
 * End-to-end test for multi-declarator GLSL declarations in the `gui` WebGL
 * backend.
 *
 * `uniform sampler2D a, b;` and `uniform vec3 x, y;` are ordinary GLSL, but the
 * rewriter that turns WebGL GLSL into Vulkan GLSL used to emit only the first
 * declarator: the block members and the sampler bindings for the rest were
 * dropped, so the shader failed to compile with `'b' : undeclared identifier`.
 * three.js's materials rely on this form (`uniform sampler2D tSrc, tDepthHalf;`
 * in a post pass), so the case draws with every declarator bound and reads the
 * pixel back.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>decls</title></head>
<body style="margin:0">
  <canvas id="c" width="32" height="32"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — GLSL multi-declarator declarations", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("declares and binds every declarator of a shared statement", () => {
    const { value } = harness.compileAndRun(
      "decls",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "decls", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        const fail = (name: string, log: string): void => {
          label(name, "false:" + log);
          win.close();
        };
        if (gl == null) {
          fail("gl", "no context");
          return;
        }
        /* One statement, several declarators, on both sides of the API:
         * samplers, uniforms and varyings. */
        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "attribute vec2 a_pos, a_uv; varying vec2 v_a, v_b;" +
          "uniform float u_scale, u_bias;" +
          "void main() { v_a = a_uv; v_b = a_uv; gl_Position = vec4(a_pos * u_scale + u_bias, 0.0, 1.0); }");
        gl.compileShader(vs);
        if (!gl.getShaderParameter(vs, gl.COMPILE_STATUS)) {
          fail("vs", gl.getShaderInfoLog(vs));
          return;
        }
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "precision mediump float; varying vec2 v_a, v_b;" +
          "uniform sampler2D u_first, u_second;" +
          "uniform vec3 u_tint, u_fill; uniform float u_mix, u_alpha;" +
          "void main() { vec3 a = texture2D(u_first, v_a).rgb * u_tint;" +
          " vec3 b = texture2D(u_second, v_b).rgb * u_fill;" +
          " gl_FragColor = vec4(mix(a, b, u_mix), u_alpha); }");
        gl.compileShader(fs);
        if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS)) {
          fail("fs", gl.getShaderInfoLog(fs));
          return;
        }
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
          fail("link", gl.getProgramInfoLog(program));
          return;
        }
        label("link", "true");
        gl.useProgram(program);

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1, 0, 0,  3, -1, 2, 0,  -1, 3, 0, 2,
        ]), gl.STATIC_DRAW);
        const pos: any = gl.getAttribLocation(program, "a_pos");
        const uv: any = gl.getAttribLocation(program, "a_uv");
        label("attrs", pos + "," + uv);
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 16, 0);
        gl.enableVertexAttribArray(uv);
        gl.vertexAttribPointer(uv, 2, gl.FLOAT, false, 16, 8);

        /* Two 1x1 white textures: only the second one's binding matters for the
         * mixed result, so a wrong binding makes the pixel red/black instead of
         * green. */
        const makeTexture = (): any => {
          const texture: any = gl.createTexture();
          gl.bindTexture(gl.TEXTURE_2D, texture);
          gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
            new Uint8Array([255, 255, 255, 255]));
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
          return texture;
        };
        const first: any = makeTexture();
        const second: any = makeTexture();
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, first);
        gl.activeTexture(gl.TEXTURE1);
        gl.bindTexture(gl.TEXTURE_2D, second);
        gl.uniform1i(gl.getUniformLocation(program, "u_first"), 0);
        label("loc-second", gl.getUniformLocation(program, "u_second") != null);
        gl.uniform1i(gl.getUniformLocation(program, "u_second"), 1);
        /* u_scale/u_bias and u_tint/u_fill are shared statements too. */
        gl.uniform1f(gl.getUniformLocation(program, "u_scale"), 1.0);
        gl.uniform1f(gl.getUniformLocation(program, "u_bias"), 0.0);
        gl.uniform3f(gl.getUniformLocation(program, "u_tint"), 1, 0, 0);
        gl.uniform3f(gl.getUniformLocation(program, "u_fill"), 0, 1, 0);
        gl.uniform1f(gl.getUniformLocation(program, "u_mix"), 1.0);
        gl.uniform1f(gl.getUniformLocation(program, "u_alpha"), 1.0);

        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        const pixels = new Uint8Array(4);
        gl.readPixels(16, 16, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        label("pixel", pixels[0] + "," + pixels[1] + "," + pixels[2] + "," + pixels[3]);
        win.close();
      });
      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 8000 },
    );
    expect(value("link")).toBe("true");
    expect(value("loc-second")).toBe("true");
    /* Mixed toward `u_second` (green) with `u_fill`, proving the second sampler
     * of the shared declaration is bound to unit 1. */
    expect(value("pixel")).toBe("0,255,0,255");
  });
});
