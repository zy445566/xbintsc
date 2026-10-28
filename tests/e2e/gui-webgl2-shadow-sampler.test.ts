/**
 * GUI e2e: comparative shadow samplers and `gl_FragDepth`.
 *
 * `sampler2DShadow` must reach SDL_GPU as a comparison sampler, and a fragment
 * shader that writes `gl_FragDepth` must actually update the depth attachment.
 * Both are checked by rendering and reading pixels back.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl2-shadow-sampler</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — shadow samplers and gl_FragDepth", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("samples a depth texture through sampler2DShadow", () => {
    const { value } = harness.compileAndRun(
      "webgl2-shadow-sampler",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "shadow-sampler", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }
        const px = new Uint8Array(4);
        const probe = (): string => {
          gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };

        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);

        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\n" +
          "precision highp float;\\n" +
          "uniform highp sampler2DShadow u_depth;\\n" +
          "uniform highp float u_ref;\\n" +
          "out highp vec4 fragColor;\\n" +
          "void main() {\\n" +
          "  float v = texture(u_depth, vec3(0.5, 0.5, u_ref));\\n" +
          "  fragColor = vec4(v, v, v, 1.0);\\n" +
          "}");
        gl.compileShader(fs);
        const fs_ok = gl.getShaderParameter(fs, gl.COMPILE_STATUS);
        label("fs", fs_ok === true ? "true" : "false:" + gl.getShaderInfoLog(fs));

        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        label("link", gl.getProgramParameter(program, gl.LINK_STATUS) === true);

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
        gl.useProgram(program);
        const pos: any = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);

        // A depth texture cleared to 0.5, attached to a 16x16 framebuffer.
        const depthTex: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, depthTex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.DEPTH_COMPONENT24, 16, 16, 0,
          gl.DEPTH_COMPONENT, gl.UNSIGNED_INT, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_MODE, gl.COMPARE_REF_TO_TEXTURE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_FUNC, gl.LEQUAL);
        label("compare-mode", gl.getTexParameter(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_MODE));
        label("compare-func", gl.getTexParameter(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_FUNC));

        const colorTex: any = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, colorTex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 16, 16, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);

        const fb: any = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, colorTex, 0);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, depthTex, 0);
        label("framebuffer", gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
        gl.viewport(0, 0, 16, 16);
        gl.clearColor(0, 0, 0, 1);
        gl.clearDepth(0.5);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, 64, 64);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, depthTex);
        gl.uniform1i(gl.getUniformLocation(program, "u_depth"), 0);
        const ref: any = gl.getUniformLocation(program, "u_ref");

        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.uniform1f(ref, 0.25);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("passes", probe());

        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.uniform1f(ref, 0.75);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("fails", probe());
        label("error", gl.getError());
        win.close();
      });
      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE } },
    );

    expect(value("fs")).toBe("true");
    expect(value("link")).toBe("true");
    expect(value("compare-mode")).toBe("34894");  // COMPARE_REF_TO_TEXTURE, 0x884e
    expect(value("compare-func")).toBe("515");    // LEQUAL, 0x0203
    expect(value("framebuffer")).toBe("true");
    // ref 0.25 <= stored 0.5 -> pass (white); ref 0.75 > 0.5 -> fail (black).
    expect(value("passes")).toBe("255,255,255,255");
    expect(value("fails")).toBe("0,0,0,255");
    expect(value("error")).toBe("0");
  });

  it("writes gl_FragDepth into the depth attachment", () => {
    const { value } = harness.compileAndRun(
      "webgl2-frag-depth",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "frag-depth", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }
        const px = new Uint8Array(4);
        const probe = (): string => {
          gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };

        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; uniform float u_z;" +
          "void main() { gl_Position = vec4(a_pos, u_z, 1.0); }");
        gl.compileShader(vs);

        const depth_fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(depth_fs, "#version 300 es\\n" +
          "precision highp float; out vec4 fragColor;\\n" +
          "void main() { gl_FragDepth = 0.25; fragColor = vec4(0.0, 0.0, 0.0, 1.0); }");
        gl.compileShader(depth_fs);
        label("depth-fs", gl.getShaderParameter(depth_fs, gl.COMPILE_STATUS) === true);

        const red_fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(red_fs, "#version 300 es\\n" +
          "precision highp float; out vec4 fragColor;\\n" +
          "void main() { fragColor = vec4(1.0, 0.0, 0.0, 1.0); }");
        gl.compileShader(red_fs);

        const depth_program: any = gl.createProgram();
        gl.attachShader(depth_program, vs);
        gl.attachShader(depth_program, depth_fs);
        gl.linkProgram(depth_program);
        label("depth-link", gl.getProgramParameter(depth_program, gl.LINK_STATUS) === true);

        const red_program: any = gl.createProgram();
        gl.attachShader(red_program, vs);
        gl.attachShader(red_program, red_fs);
        gl.linkProgram(red_program);
        label("red-link", gl.getProgramParameter(red_program, gl.LINK_STATUS) === true);

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);

        gl.viewport(0, 0, 64, 64);
        gl.clearColor(0, 0, 0, 1);
        gl.clearDepth(1.0);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.ALWAYS);
        gl.depthMask(true);
        gl.useProgram(depth_program);
        let pos: any = gl.getAttribLocation(depth_program, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        gl.uniform1f(gl.getUniformLocation(depth_program, "u_z"), 0.0);
        gl.drawArrays(gl.TRIANGLES, 0, 3);

        // z = 0.5 is farther than the 0.25 the first pass wrote, so LESS fails.
        gl.depthFunc(gl.LESS);
        gl.depthMask(false);
        gl.useProgram(red_program);
        pos = gl.getAttribLocation(red_program, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        gl.uniform1f(gl.getUniformLocation(red_program, "u_z"), 0.5);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("occluded", probe());

        gl.depthMask(true);
        gl.disable(gl.DEPTH_TEST);
        label("error", gl.getError());
        win.close();
      });
      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE } },
    );

    expect(value("depth-fs")).toBe("true");
    expect(value("depth-link")).toBe("true");
    expect(value("red-link")).toBe("true");
    // If gl_FragDepth were ignored the depth stayed 1.0 and the red quad would
    // have painted over the black first pass.
    expect(value("occluded")).toBe("0,0,0,255");
    expect(value("error")).toBe("0");
  });

  it("copies a depth attachment with blitFramebuffer", () => {
    const { value } = harness.compileAndRun(
      "webgl2-depth-blit",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "depth-blit", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }
        const px = new Uint8Array(4);
        const probe = (x: number, y: number): string => {
          gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };
        const make_program = (vs_source: string, fs_source: string): any => {
          const vs: any = gl.createShader(gl.VERTEX_SHADER);
          gl.shaderSource(vs, vs_source);
          gl.compileShader(vs);
          const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
          gl.shaderSource(fs, fs_source);
          gl.compileShader(fs);
          const program: any = gl.createProgram();
          gl.attachShader(program, vs);
          gl.attachShader(program, fs);
          gl.linkProgram(program);
          return program;
        };
        const show = make_program(
          "#version 300 es\\nin vec2 a_pos; out vec2 v_uv;" +
          "void main() { v_uv = a_pos * 0.5 + 0.5;" +
          "  gl_Position = vec4(a_pos, 0.0, 1.0); }",
          "#version 300 es\\nprecision highp float; in vec2 v_uv;" +
          "uniform sampler2D u_tex; out vec4 fragColor;" +
          "void main() { fragColor = vec4(vec3(texture(u_tex, v_uv).r), 1.0); }");
        label("show-link", gl.getProgramParameter(show, gl.LINK_STATUS) === true);

        const make_target = (depth_value: number): any[] => {
          const depth: any = gl.createTexture();
          gl.bindTexture(gl.TEXTURE_2D, depth);
          gl.texImage2D(gl.TEXTURE_2D, 0, gl.DEPTH_COMPONENT24, 16, 16, 0,
            gl.DEPTH_COMPONENT, gl.UNSIGNED_INT, null);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
          const color: any = gl.createTexture();
          gl.bindTexture(gl.TEXTURE_2D, color);
          gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 16, 16, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
          const fb: any = gl.createFramebuffer();
          gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
          gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, color, 0);
          gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, depth, 0);
          gl.viewport(0, 0, 16, 16);
          gl.clearColor(0, 0, 0, 1);
          gl.clearDepth(depth_value);
          gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
          return [fb, color, depth];
        };
        const source = make_target(0.25);
        const destination = make_target(1.0);
        label("complete",
          gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);

        // Copy only the left half of the source into the right half of the
        // destination; the destination's left half must keep depth 1.0.
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, source[0]);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, destination[0]);
        gl.blitFramebuffer(0, 0, 8, 16, 8, 0, 16, 16, gl.DEPTH_BUFFER_BIT, gl.NEAREST);
        label("blit-error", gl.getError());

        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, 64, 64);
        gl.useProgram(show);
        gl.uniform1i(gl.getUniformLocation(show, "u_tex"), 0);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, destination[2]);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
        const pos: any = gl.getAttribLocation(show, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("untouched", probe(16, 32));
        label("copied", probe(48, 32));
        win.close();
      });
      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE } },
    );

    expect(value("show-link")).toBe("true");
    expect(value("complete")).toBe("true");
    expect(value("blit-error")).toBe("0");
    // 1.0 stays white; the copied 0.25 depth reads back near 64.
    expect(value("untouched")).toBe("255,255,255,255");
    const copied = value("copied").split(",").map(Number);
    expect(copied[0] ?? 0).toBeGreaterThan(55);
    expect(copied[0] ?? 0).toBeLessThan(75);
  });
});
