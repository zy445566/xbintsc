/**
 * End-to-end test for the `gui` extension's WebGL 1.0 context.
 *
 * The program draws on a `<canvas>` through `getContext("webgl")` and reads the
 * drawing buffer back with `readPixels`, so a pass proves that the shaders were
 * translated for the platform's backend — SPIR-V on Vulkan, MSL on Metal — the
 * pipelines were created, and the draw really executed on the GPU. It also
 * paints the canvas into the page (the engine's image pipeline samples the
 * WebGL colour texture directly).
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

  it("supports blend constants, dynamic buffers, vertex textures, gl_FragDepth diagnostics and context loss", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl-extra",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "webgl-extra", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);
      const rgba = (pixels: any): string =>
        pixels[0] + "," + pixels[1] + "," + pixels[2] + "," + pixels[3];

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl");
        if (gl == null) {
          console.log("webgl=false");
          win.close();
          return;
        }
        let lost_event = false;
        let restored_event = false;
        canvas.addEventListener("webglcontextlost", () => { lost_event = true; });
        canvas.addEventListener("webglcontextrestored", () => { restored_event = true; });

        canvas.width = 32;
        canvas.height = 32;
        gl.viewport(0, 0, 32, 32);
        const pixels = new Uint8Array(4);
        const probe = (): string => {
          gl.readPixels(16, 16, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
          return rgba(pixels);
        };

        label("vertex-units", gl.getParameter(gl.MAX_VERTEX_TEXTURE_IMAGE_UNITS));

        // --- constant blend colour -----------------------------------------
        label("blend-color-default", gl.getParameter(gl.BLEND_COLOR).join(","));
        gl.blendColor(0, 1, 0, 1);
        label("blend-color", gl.getParameter(gl.BLEND_COLOR).join(","));

        const simple_vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(simple_vs, "attribute vec2 a_pos;" +
          "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(simple_vs);
        const simple_fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(simple_fs, "precision mediump float; uniform vec4 u_color;" +
          "void main() { gl_FragColor = u_color; }");
        gl.compileShader(simple_fs);
        const simple: any = gl.createProgram();
        gl.attachShader(simple, simple_vs);
        gl.attachShader(simple, simple_fs);
        gl.linkProgram(simple);
        label("simple-link", gl.getProgramParameter(simple, gl.LINK_STATUS));
        gl.useProgram(simple);

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1,  3, -1,  -1, 3,
        ]), gl.DYNAMIC_DRAW);
        label("buffer-usage", gl.getBufferParameter(gl.ARRAY_BUFFER, gl.BUFFER_USAGE));
        const pos: any = gl.getAttribLocation(simple, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        const color_loc: any = gl.getUniformLocation(simple, "u_color");

        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.CONSTANT_COLOR, gl.ZERO);
        gl.uniform4f(color_loc, 1, 1, 1, 1);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("blend-constant", probe());
        gl.disable(gl.BLEND);

        // --- vertex-stage texture sampling --------------------------------
        const vtex_vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vtex_vs, "attribute vec2 a_pos; uniform sampler2D u_vtex;" +
          "varying vec4 v_tint;" +
          "void main() { v_tint = texture2D(u_vtex, vec2(0.5, 0.5));" +
          "  gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vtex_vs);
        label("vtex-vs", gl.getShaderParameter(vtex_vs, gl.COMPILE_STATUS));
        const vtex_fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(vtex_fs, "precision mediump float; varying vec4 v_tint;" +
          "void main() { gl_FragColor = v_tint; }");
        gl.compileShader(vtex_fs);
        const vtex: any = gl.createProgram();
        gl.attachShader(vtex, vtex_vs);
        gl.attachShader(vtex, vtex_fs);
        gl.linkProgram(vtex);
        const vtex_link = gl.getProgramParameter(vtex, gl.LINK_STATUS);
        label("vtex-link", vtex_link === true ? "true" : "false:" + gl.getProgramInfoLog(vtex));

        const texture: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA, gl.UNSIGNED_BYTE,
          new Uint8Array([0, 255, 0, 255, 0, 255, 0, 255,
                          0, 255, 0, 255, 0, 255, 0, 255]));
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

        gl.useProgram(vtex);
        gl.uniform1i(gl.getUniformLocation(vtex, "u_vtex"), 0);
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.enableVertexAttribArray(gl.getAttribLocation(vtex, "a_pos"));
        gl.vertexAttribPointer(gl.getAttribLocation(vtex, "a_pos"), 2, gl.FLOAT, false, 8, 0);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("vertex-texture", probe());

        // --- shader-written depth is diagnosed -----------------------------
        const depth_fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(depth_fs, "precision mediump float;" +
          "void main() { gl_FragDepth = 0.5; gl_FragColor = vec4(1.0); }");
        gl.compileShader(depth_fs);
        label("fragdepth", gl.getShaderParameter(depth_fs, gl.COMPILE_STATUS));
        label("fragdepth-log", gl.getShaderInfoLog(depth_fs));

        // --- context loss and restore --------------------------------------
        const ext: any = gl.getExtension("WEBGL_lose_context");
        label("lose-ext", ext != null);
        ext.loseContext();
        label("lost", gl.isContextLost());
        label("lost-error", gl.getError());
        label("lost-create", gl.createBuffer() == null);
        label("lost-event", lost_event);
        ext.restoreContext();
        label("restored", gl.isContextLost());
        label("restored-event", restored_event);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(0, 0, 1, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        label("after-restore", probe());
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 8000 },
    );

    expect(value("vertex-units")).toBe("16");
    expect(value("blend-color-default")).toBe("0,0,0,0");
    expect(value("blend-color")).toBe("0,1,0,1");
    expect(value("simple-link")).toBe("true");
    expect(value("buffer-usage")).toBe("35048"); // GL_DYNAMIC_DRAW
    expect(value("blend-constant")).toBe("0,255,0,255");
    expect(value("vtex-vs")).toBe("true");
    expect(value("vtex-link")).toBe("true");
    expect(value("vertex-texture")).toBe("0,255,0,255");
    expect(value("fragdepth")).toBe("false");
    expect(value("fragdepth-log")).toContain("gl_FragDepth");
    expect(value("lose-ext")).toBe("true");
    expect(value("lost")).toBe("true");
    expect(value("lost-error")).toBe("37442"); // GL_CONTEXT_LOST_WEBGL
    expect(value("lost-create")).toBe("true");
    expect(value("lost-event")).toBe("true");
    expect(value("restored")).toBe("false");
    expect(value("restored-event")).toBe("true");
    expect(value("after-restore")).toBe("0,0,255,255");
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });

  it("samples a cube map face by direction", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl-cube-map",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "cube-map", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl");
        if (gl == null) {
          console.log("webgl=false");
          win.close();
          return;
        }
        label("max-cube", gl.getParameter(gl.MAX_CUBE_MAP_TEXTURE_SIZE));
        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "attribute vec2 a_pos; varying vec2 v_uv;" +
          "void main() { v_uv = a_pos * 0.5 + 0.5;" +
          "  gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "precision mediump float; varying vec2 v_uv;" +
          "uniform samplerCube u_cube; uniform vec3 u_dir;" +
          "void main() { gl_FragColor = textureCube(u_cube, u_dir); }");
        gl.compileShader(fs);
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        label("link", gl.getProgramParameter(program, gl.LINK_STATUS));
        gl.useProgram(program);
        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1,  3, -1,  -1, 3,
        ]), gl.STATIC_DRAW);
        const pos: any = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);

        const faces = [
          gl.TEXTURE_CUBE_MAP_POSITIVE_X, gl.TEXTURE_CUBE_MAP_NEGATIVE_X,
          gl.TEXTURE_CUBE_MAP_POSITIVE_Y, gl.TEXTURE_CUBE_MAP_NEGATIVE_Y,
          gl.TEXTURE_CUBE_MAP_POSITIVE_Z, gl.TEXTURE_CUBE_MAP_NEGATIVE_Z,
        ];
        const colors = [
          [255, 0, 0, 255], [0, 255, 0, 255], [0, 0, 255, 255],
          [255, 255, 0, 255], [255, 0, 255, 255], [0, 255, 255, 255],
        ];
        const cube: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_CUBE_MAP, cube);
        for (let i = 0; i < 6; i++) {
          gl.texImage2D(faces[i], 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
            new Uint8Array(colors[i]));
        }
        gl.texParameteri(gl.TEXTURE_CUBE_MAP, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_CUBE_MAP, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        label("binding", gl.getParameter(gl.TEXTURE_BINDING_CUBE_MAP) != null);
        gl.uniform1i(gl.getUniformLocation(program, "u_cube"), 0);
        const dir: any = gl.getUniformLocation(program, "u_dir");
        const px = new Uint8Array(4);
        const sample = (x: number, y: number, z: number): string => {
          gl.uniform3f(dir, x, y, z);
          gl.clear(gl.COLOR_BUFFER_BIT);
          gl.drawArrays(gl.TRIANGLES, 0, 3);
          gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };
        gl.viewport(0, 0, 64, 64);
        label("face-x", sample(1, 0, 0));
        label("face-neg-x", sample(-1, 0, 0));
        label("face-y", sample(0, 1, 0));
        label("face-neg-y", sample(0, -1, 0));
        label("face-z", sample(0, 0, 1));
        label("face-neg-z", sample(0, 0, -1));
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(value("max-cube")).toBe("8192");
    expect(value("link")).toBe("true");
    expect(value("binding")).toBe("true");
    expect(value("face-x")).toBe("255,0,0,255");
    expect(value("face-neg-x")).toBe("0,255,0,255");
    expect(value("face-y")).toBe("0,0,255,255");
    expect(value("face-neg-y")).toBe("255,255,0,255");
    expect(value("face-z")).toBe("255,0,255,255");
    expect(value("face-neg-z")).toBe("0,255,255,255");
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });
});
