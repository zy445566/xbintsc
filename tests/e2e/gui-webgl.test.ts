/**
 * End-to-end test for the `gui` extension's WebGL 1.0 context.
 *
 * The program draws on a `<canvas>` through `getContext("webgl")` and reads the
 * drawing buffer back with `readPixels`, so a pass proves that the shaders were
 * translated to SPIR-V, the pipelines were created, and the draw really
 * executed on the GPU. It also paints the canvas into the page (the engine's
 * image pipeline samples the WebGL colour texture directly).
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("compiles GLSL, draws on the GPU and reads the pixels back", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "webgl", width: 320, height: 240 });

      const label = (name: string, value: any): void => console.log(name + "=" + value);
      const rgba = (pixels: any): string =>
        pixels[0] + "," + pixels[1] + "," + pixels[2] + "," + pixels[3];

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl");
        label("webgl", gl != null);
        if (gl == null) {
          win.close();
          return;
        }
        label("version", gl.getParameter(gl.VERSION));
        label("webgl2", canvas.getContext("webgl2") == null);
        label("precision", gl.getShaderPrecisionFormat(gl.FRAGMENT_SHADER, gl.HIGH_FLOAT).precision);
        label("attrs", gl.getContextAttributes().alpha);
        label("lost", gl.isContextLost());
        gl.stencilFunc(gl.ALWAYS, 0, 255);

        canvas.width = 32;
        canvas.height = 32;
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(1, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        const pixels = new Uint8Array(4);
        gl.readPixels(16, 16, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        label("clear", rgba(pixels));

        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "attribute vec2 a_pos; attribute vec2 a_uv; varying vec2 v_uv;" +
          "uniform float u_z;" +
          "void main() { v_uv = a_uv; gl_Position = vec4(a_pos, u_z, 1.0); }");
        gl.compileShader(vs);
        label("vs", gl.getShaderParameter(vs, gl.COMPILE_STATUS));
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "precision mediump float; varying vec2 v_uv; uniform vec4 u_color;" +
          "uniform sampler2D u_tex; void main() { gl_FragColor = u_color * texture2D(u_tex, v_uv); }");
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

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1, 0, 0,  3, -1, 2, 0,  -1, 3, 0, 2,
        ]), gl.STATIC_DRAW);
        const pos: any = gl.getAttribLocation(program, "a_pos");
        const uv: any = gl.getAttribLocation(program, "a_uv");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 16, 0);
        gl.enableVertexAttribArray(uv);
        gl.vertexAttribPointer(uv, 2, gl.FLOAT, false, 16, 8);
        gl.uniform4f(gl.getUniformLocation(program, "u_color"), 0, 0, 1, 1);

        const texture: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA, gl.UNSIGNED_BYTE,
          new Uint8Array([255, 255, 255, 255, 128, 128, 128, 255,
                          64, 64, 64, 255, 32, 32, 32, 255]));
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.uniform1i(gl.getUniformLocation(program, "u_tex"), 0);

        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        gl.readPixels(16, 16, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        label("triangle", rgba(pixels));

        // Depth test: draw the near quad first, then a far one; the far one
        // must not overwrite it.
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LESS);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        const zquad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, zquad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1, 0, 0,  3, -1, 2, 0,  -1, 3, 0, 2,
        ]), gl.STATIC_DRAW);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 16, 0);
        gl.vertexAttribPointer(uv, 2, gl.FLOAT, false, 16, 8);
        const z_loc: any = gl.getUniformLocation(program, "u_z");
        gl.uniform1f(z_loc, 0.0);
        gl.uniform4f(gl.getUniformLocation(program, "u_color"), 0, 1, 0, 1);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        gl.uniform1f(z_loc, 0.5);
        gl.uniform4f(gl.getUniformLocation(program, "u_color"), 1, 0, 0, 1);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        gl.readPixels(16, 16, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        label("depth", rgba(pixels));

        // Face culling: the full-screen triangle is counter-clockwise, so with
        // back-face culling and CCW front faces it must draw, and with CW front
        // faces it must be culled.
        gl.enable(gl.CULL_FACE);
        gl.cullFace(gl.BACK);
        gl.frontFace(gl.CCW);
        gl.clearColor(1, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.uniform1f(z_loc, 0.0);
        gl.uniform4f(gl.getUniformLocation(program, "u_color"), 0, 0, 1, 1);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        gl.readPixels(16, 16, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        label("cull-ccw", rgba(pixels));
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.frontFace(gl.CW);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        gl.readPixels(16, 16, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        label("cull-cw", rgba(pixels));
        gl.disable(gl.CULL_FACE);

        label("error", gl.getError());
        label("drawing-buffer", canvas.width + "x" + canvas.height);
        win.close();
      });

      win.loadHTML(page);
      console.log("loaded");
      `,
      { files: { "page.html": PAGE }, autocloseMs: 8000 },
    );

    expect(value("webgl")).toBe("true");
    expect(value("version")).toContain("WebGL 1.0");
    expect(value("webgl2")).toBe("true");
    expect(value("precision")).toBe("23");
    expect(value("attrs")).toBe("true");
    expect(value("lost")).toBe("false");
    expect(value("clear")).toBe("255,0,0,255");
    expect(value("vs")).toBe("true");
    expect(value("fs")).toBe("true");
    expect(value("link")).toBe("true");
    // The 2x2 grayscale texture modulates the blue uniform; the centre samples
    // the average of its four texels.
    expect(value("triangle")).toMatch(/^0,0,[1-9][0-9]*,255$/);
    expect(value("depth")).toMatch(/^0,[1-9][0-9]*,0,255$/);
    expect(value("cull-ccw")).toMatch(/^0,0,[1-9][0-9]*,255$/);
    expect(value("cull-cw")).toBe("255,0,0,255");
    expect(value("error")).toBe("0");
    expect(value("drawing-buffer")).toBe("32x32");
    expect(stderr).not.toContain("pipeline creation failed");
    expect(stderr).not.toContain("shader build");
  });
});
