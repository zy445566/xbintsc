/**
 * GUI e2e: sampler arrays through the WebGL 2 reflection API.
 *
 * three.js builds its uniform uploaders from `gl.getActiveUniform`: a sampler
 * declared as an array must be reported as `name[0]` with its element count,
 * or three.js treats the array as one texture and binds the array object
 * itself (a shadow map then reads the renderer's empty texture and every lit
 * surface turns black). The declaration size is spelled with a macro, as
 * three.js does for `directionalShadowMap[NUM_DIR_LIGHT_SHADOWS]`.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl2-sampler-array</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — sampler arrays and reflection", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("reports array names/sizes and samples each element", () => {
    const { value } = harness.compileAndRun(
      "webgl2-sampler-array",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "sampler-array", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          label("webgl2", false);
          win.close();
          return;
        }
        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; out vec2 v_uv;\\n" +
          "void main() { v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\n" +
          "precision highp float;\\n" +
          "#define MAP_COUNT 1\\n" +
          "uniform sampler2D u_literal[1];\\n" +
          "uniform sampler2D u_macro[1];\\n" +
          "in vec2 v_uv; out vec4 fragColor;\\n" +
          "void main() {\\n" +
          "  fragColor = vec4(texture(u_literal[0], v_uv).r, texture(u_macro[0], v_uv).g, 0.0, 1.0);\\n" +
          "}");
        gl.compileShader(fs);
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        label("linked", gl.getProgramParameter(program, gl.LINK_STATUS));
        gl.useProgram(program);

        const count: number = gl.getProgramParameter(program, gl.ACTIVE_UNIFORMS);
        let macroName = "";
        let macroSize = 0;
        for (let i = 0; i < count; i++) {
          const info: any = gl.getActiveUniform(program, i);
          if (info != null && info.name.indexOf("u_macro") === 0) {
            macroName = info.name;
            macroSize = info.size;
          }
        }
        label("macro-name", macroName);
        label("macro-size", macroSize);
        label("macro-location", gl.getUniformLocation(program, "u_macro[0]") != null);

        const make = (rgba: number[]): any => {
          const texture: any = gl.createTexture();
          gl.activeTexture(gl.TEXTURE0);
          gl.bindTexture(gl.TEXTURE_2D, texture);
          gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
            new Uint8Array(rgba));
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
          return texture;
        };
        const green: any = make([0, 255, 0, 255]);

        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, green);
        gl.uniform1iv(gl.getUniformLocation(program, "u_macro[0]"), [0]);

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
    expect(value("linked")).toBe("true");
    expect(value("macro-name")).toBe("u_macro[0]");
    expect(value("macro-size")).toBe("1");
    expect(value("macro-location")).toBe("true");
    expect(value("pixel")).toBe("0,255,0,255");
  });
});
