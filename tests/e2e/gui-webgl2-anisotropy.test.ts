/**
 * GUI e2e: `EXT_texture_filter_anisotropic`.
 *
 * three.js asks for the extension and sizes `texture.anisotropy` from
 * `MAX_TEXTURE_MAX_ANISOTROPY_EXT`. Without it every grazing-angle surface
 * (wet ground, walls) falls back to a high mip level and reads as coarse
 * blocks, so the extension, the parameter clamp and sampling are checked
 * here.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl2-anisotropy</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — anisotropic filtering", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("exposes the extension, clamps the parameter and samples", () => {
    const { value } = harness.compileAndRun(
      "webgl2-anisotropy",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "anisotropy", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          label("webgl2", false);
          win.close();
          return;
        }
        const ext: any = gl.getExtension("EXT_texture_filter_anisotropic");
        label("extension", ext != null);
        if (ext == null) {
          win.close();
          return;
        }
        label("max", gl.getParameter(ext.MAX_TEXTURE_MAX_ANISOTROPY_EXT));
        label("supported",
          gl.getSupportedExtensions().indexOf("EXT_texture_filter_anisotropic") >= 0);

        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; out vec2 v_uv; void main() { v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\n" +
          "precision highp float; uniform sampler2D u_tex; in vec2 v_uv; out vec4 o;\\n" +
          "void main() { o = texture(u_tex, v_uv); }");
        gl.compileShader(fs);
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        gl.useProgram(program);

        const texture: any = gl.createTexture();
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
          new Uint8Array([0, 255, 0, 255]));
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameterf(gl.TEXTURE_2D, ext.TEXTURE_MAX_ANISOTROPY_EXT, 8);
        label("read8", gl.getTexParameter(gl.TEXTURE_2D, ext.TEXTURE_MAX_ANISOTROPY_EXT));
        gl.texParameterf(gl.TEXTURE_2D, ext.TEXTURE_MAX_ANISOTROPY_EXT, 32);
        label("clampHigh", gl.getTexParameter(gl.TEXTURE_2D, ext.TEXTURE_MAX_ANISOTROPY_EXT));
        gl.texParameterf(gl.TEXTURE_2D, ext.TEXTURE_MAX_ANISOTROPY_EXT, 0);
        label("clampLow", gl.getTexParameter(gl.TEXTURE_2D, ext.TEXTURE_MAX_ANISOTROPY_EXT));
        gl.texParameterf(gl.TEXTURE_2D, ext.TEXTURE_MAX_ANISOTROPY_EXT, 4);

        gl.uniform1i(gl.getUniformLocation(program, "u_tex"), 0);
        const buffer: any = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
        const loc: any = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(loc);
        gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
        gl.viewport(0, 0, 64, 64);
        gl.clearColor(0, 0, 0, 0);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        const px = new Uint8Array(4);
        gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        label("pixel", px[0] + "," + px[1] + "," + px[2] + "," + px[3]);
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE } },
    );
    expect(value("extension")).toBe("true");
    expect(value("max")).toBe("16");
    expect(value("supported")).toBe("true");
    expect(value("read8")).toBe("8");
    expect(value("clampHigh")).toBe("16");
    expect(value("clampLow")).toBe("1");
    expect(value("pixel")).toBe("0,255,0,255");
  });
});
