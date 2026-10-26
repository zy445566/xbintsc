/**
 * End-to-end tests for WebGL 2 3D and 2D-array textures.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl2-3d</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL 2 3D textures", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("samples 2D-array and 3D texture layers", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-3d",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "3d", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }
        label("max-3d", gl.getParameter(gl.MAX_3D_TEXTURE_SIZE));
        label("max-layers", gl.getParameter(gl.MAX_ARRAY_TEXTURE_LAYERS));

        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; out vec2 v_uv;" +
          "void main() { v_uv = a_pos * 0.5 + 0.5;" +
          "  gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const make_program = (fs_source: string): any => {
          const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
          gl.shaderSource(fs, fs_source);
          gl.compileShader(fs);
          const program: any = gl.createProgram();
          gl.attachShader(program, vs);
          gl.attachShader(program, fs);
          gl.linkProgram(program);
          return program;
        };
        const array_program: any = make_program("#version 300 es\\n" +
          "precision highp float; precision highp sampler2DArray;" +
          "in vec2 v_uv; uniform sampler2DArray u_arr; uniform float u_layer;" +
          "out vec4 fragColor;" +
          "void main() { fragColor = texture(u_arr, vec3(v_uv, u_layer)); }");
        const volume_program: any = make_program("#version 300 es\\n" +
          "precision highp float; precision highp sampler3D;" +
          "in vec2 v_uv; uniform sampler3D u_vol; uniform float u_z;" +
          "out vec4 fragColor;" +
          "void main() { fragColor = texture(u_vol, vec3(v_uv, u_z)); }");
        label("programs", gl.getProgramParameter(array_program, gl.LINK_STATUS) === true &&
          gl.getProgramParameter(volume_program, gl.LINK_STATUS) === true);

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1,  3, -1,  -1, 3,
        ]), gl.STATIC_DRAW);
        const bind_quad = (program: any): void => {
          gl.useProgram(program);
          const pos: any = gl.getAttribLocation(program, "a_pos");
          gl.enableVertexAttribArray(pos);
          gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        };
        const px = new Uint8Array(4);
        const probe = (): string => {
          gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };
        const red = [255, 0, 0, 255];
        const green = [0, 255, 0, 255];
        const layer = (color: number[]): number[] => {
          return color.concat(color).concat(color).concat(color);
        };
        const data = new Uint8Array(layer(red).concat(layer(green)));

        gl.viewport(0, 0, 64, 64);

        // --- 2D array -----------------------------------------------------
        const array: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D_ARRAY, array);
        gl.texImage3D(gl.TEXTURE_2D_ARRAY, 0, gl.RGBA, 2, 2, 2, 0,
          gl.RGBA, gl.UNSIGNED_BYTE, data);
        gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        bind_quad(array_program);
        gl.uniform1i(gl.getUniformLocation(array_program, "u_arr"), 0);
        const array_layer: any = gl.getUniformLocation(array_program, "u_layer");
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.uniform1f(array_layer, 0);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("array-0", probe());
        gl.uniform1f(array_layer, 1);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("array-1", probe());

        // --- 3D -----------------------------------------------------------
        const volume: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_3D, volume);
        gl.texImage3D(gl.TEXTURE_3D, 0, gl.RGBA, 2, 2, 2, 0,
          gl.RGBA, gl.UNSIGNED_BYTE, data);
        gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        bind_quad(volume_program);
        gl.uniform1i(gl.getUniformLocation(volume_program, "u_vol"), 0);
        const z: any = gl.getUniformLocation(volume_program, "u_z");
        gl.uniform1f(z, 0.25);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("volume-0", probe());
        gl.uniform1f(z, 0.75);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("volume-1", probe());
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(value("max-3d")).toBe("2048");
    expect(value("max-layers")).toBe("256");
    expect(value("programs")).toBe("true");
    expect(value("array-0")).toBe("255,0,0,255");
    expect(value("array-1")).toBe("0,255,0,255");
    expect(value("volume-0")).toBe("255,0,0,255");
    expect(value("volume-1")).toBe("0,255,0,255");
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });

  it("updates array layers with texSubImage3D and renders with framebufferTextureLayer", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-3d-layer",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "3d-layer", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }
        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; out vec2 v_uv;" +
          "void main() { v_uv = a_pos * 0.5 + 0.5;" +
          "  gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const flat: any = gl.createProgram();
        {
          const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
          gl.shaderSource(fs, "#version 300 es\\n" +
            "precision highp float; uniform vec4 u_color; out vec4 fragColor;" +
            "void main() { fragColor = u_color; }");
          gl.compileShader(fs);
          gl.attachShader(flat, vs);
          gl.attachShader(flat, fs);
          gl.linkProgram(flat);
        }
        const sampler: any = gl.createProgram();
        {
          const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
          gl.shaderSource(fs, "#version 300 es\\n" +
            "precision highp float; precision highp sampler2DArray;" +
            "in vec2 v_uv; uniform sampler2DArray u_arr; uniform float u_layer;" +
            "out vec4 fragColor;" +
            "void main() { fragColor = texture(u_arr, vec3(v_uv, u_layer)); }");
          gl.compileShader(fs);
          gl.attachShader(sampler, vs);
          gl.attachShader(sampler, fs);
          gl.linkProgram(sampler);
        }
        label("programs", gl.getProgramParameter(flat, gl.LINK_STATUS) === true &&
          gl.getProgramParameter(sampler, gl.LINK_STATUS) === true);

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1,  3, -1,  -1, 3,
        ]), gl.STATIC_DRAW);
        const bind = (program: any): void => {
          gl.useProgram(program);
          const pos: any = gl.getAttribLocation(program, "a_pos");
          gl.enableVertexAttribArray(pos);
          gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        };
        const px = new Uint8Array(4);
        const probe = (): string => {
          gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };
        const red = new Uint8Array(16);
        const green = new Uint8Array(16);
        for (let i = 0; i < 4; i++) {
          red[i * 4] = 255;
          red[i * 4 + 3] = 255;
          green[i * 4 + 1] = 255;
          green[i * 4 + 3] = 255;
        }

        const array: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D_ARRAY, array);
        gl.texStorage3D(gl.TEXTURE_2D_ARRAY, 1, gl.RGBA, 2, 2, 2);
        gl.texSubImage3D(gl.TEXTURE_2D_ARRAY, 0, 0, 0, 0, 2, 2, 1,
          gl.RGBA, gl.UNSIGNED_BYTE, red);
        gl.texSubImage3D(gl.TEXTURE_2D_ARRAY, 0, 0, 0, 1, 2, 2, 1,
          gl.RGBA, gl.UNSIGNED_BYTE, green);
        gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        const sample_layer = (layer: number): string => {
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.viewport(0, 0, 64, 64);
          gl.clearColor(0, 0, 0, 1);
          gl.clear(gl.COLOR_BUFFER_BIT);
          bind(sampler);
          gl.uniform1i(gl.getUniformLocation(sampler, "u_arr"), 0);
          gl.uniform1f(gl.getUniformLocation(sampler, "u_layer"), layer);
          gl.bindTexture(gl.TEXTURE_2D_ARRAY, array);
          gl.drawArrays(gl.TRIANGLES, 0, 3);
          return probe();
        };
        label("sub-0", sample_layer(0));
        label("sub-1", sample_layer(1));

        const fbo: any = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, array, 0, 1);
        label("fbo", gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
        gl.viewport(0, 0, 2, 2);
        bind(flat);
        gl.uniform4f(gl.getUniformLocation(flat, "u_color"), 0, 0, 1, 1);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("rendered", sample_layer(1));
        label("untouched", sample_layer(0));
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(value("programs")).toBe("true");
    expect(value("sub-0")).toBe("255,0,0,255");
    expect(value("sub-1")).toBe("0,255,0,255");
    expect(value("fbo")).toBe("true");
    expect(value("rendered")).toBe("0,0,255,255");
    expect(value("untouched")).toBe("255,0,0,255");
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });
});
