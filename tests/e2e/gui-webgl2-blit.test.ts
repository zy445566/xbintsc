/**
 * End-to-end tests for WebGL 2 framebuffer blits and scissored clears.
 *
 * A blit or a scissored clear only writes the region it covers; the pixels
 * around it must keep their previous contents. A destination load-op of
 * DONT_CARE breaks that on a tile-based backend (Metal's
 * MTLLoadActionDontCare discards the whole attachment), so these probes read
 * back both the written region and the untouched one.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl2-blit</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

/**
 * Compare a `readPixels` result with the expected RGBA bytes. Depth sampled
 * back through an RGBA8 target differs by one or two bytes between backends
 * (0.5 arrives as 127 or 128), so those values use a tolerance.
 */
function near(actual: string, expected: number[], tolerance: number): void {
  const parts = actual.split(",").map((part) => Number(part));
  expect(parts.length).toBe(expected.length);
  for (let i = 0; i < expected.length; i++) {
    const actual = parts[i] ?? Number.POSITIVE_INFINITY;
    const want = expected[i] ?? Number.POSITIVE_INFINITY;
    expect(Math.abs(actual - want)).toBeLessThanOrEqual(tolerance);
  }
}

describe.skipIf(!guiAvailable)("gui extension — WebGL 2 blits", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("blits framebuffers and clears a scissor rectangle", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-blit",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "blit", width: 320, height: 240 });
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
        const red = (x: number, y: number): string => {
          gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return "" + px[0];
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
        const flat = make_program(
          "#version 300 es\\nin vec2 a_pos; uniform float u_z;" +
          "void main() { gl_Position = vec4(a_pos, u_z, 1.0); }",
          "#version 300 es\\nprecision highp float; uniform vec4 u_color;" +
          "out vec4 fragColor; void main() { fragColor = u_color; }");
        const sampler = make_program(
          "#version 300 es\\nin vec2 a_pos; out vec2 v_uv;" +
          "void main() { v_uv = a_pos * 0.5 + 0.5;" +
          "  gl_Position = vec4(a_pos, 0.0, 1.0); }",
          "#version 300 es\\nprecision highp float; in vec2 v_uv;" +
          "uniform sampler2D u_tex; out vec4 fragColor;" +
          "void main() { fragColor = vec4(vec3(texture(u_tex, v_uv).r), 1.0); }");
        label("programs", gl.getProgramParameter(flat, gl.LINK_STATUS) === true &&
          gl.getProgramParameter(sampler, gl.LINK_STATUS) === true);

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          -1, -1,  3, -1,  -1, 3,
        ]), gl.STATIC_DRAW);
        const draw_flat = (r: number, g: number, b: number, z: number): void => {
          gl.useProgram(flat);
          gl.bindBuffer(gl.ARRAY_BUFFER, quad);
          gl.enableVertexAttribArray(gl.getAttribLocation(flat, "a_pos"));
          gl.vertexAttribPointer(gl.getAttribLocation(flat, "a_pos"), 2, gl.FLOAT, false, 8, 0);
          gl.uniform1f(gl.getUniformLocation(flat, "u_z"), z);
          gl.uniform4f(gl.getUniformLocation(flat, "u_color"), r, g, b, 1);
          gl.drawArrays(gl.TRIANGLES, 0, 3);
        };
        const show_depth = (depth: any): string => {
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.viewport(0, 0, 64, 64);
          gl.useProgram(sampler);
          gl.uniform1i(gl.getUniformLocation(sampler, "u_tex"), 0);
          gl.bindTexture(gl.TEXTURE_2D, depth);
          gl.clearColor(0, 0, 0, 1);
          gl.clear(gl.COLOR_BUFFER_BIT);
          gl.drawArrays(gl.TRIANGLES, 0, 3);
          return red(8, 32) + "," + red(56, 32) + "," + red(32, 8) + "," + red(32, 56);
        };

        // --- scissored colour clear ---------------------------------------
        gl.viewport(0, 0, 64, 64);
        gl.clearColor(1, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.enable(gl.SCISSOR_TEST);
        gl.scissor(8, 8, 16, 16);
        gl.clearColor(0, 0, 1, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.disable(gl.SCISSOR_TEST);
        label("scissor-inside", probe(16, 16));
        label("scissor-outside", probe(2, 2));

        // --- render to a texture and blit it back -------------------------
        const target = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, target);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 32, 32, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        const fbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, target, 0);
        label("fbo", gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(0, 1, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        draw_flat(0, 0, 1, 0);
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, fbo);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        gl.blitFramebuffer(0, 0, 32, 32, 0, 0, 64, 64, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        label("blit", probe(32, 32));

        // --- scissored depth clear ---------------------------------------
        const depth = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, depth);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.DEPTH_COMPONENT24, 32, 32, 0,
          gl.DEPTH_COMPONENT, gl.UNSIGNED_INT, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        const depth_color = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, depth_color);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 32, 32, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        const depth_fbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, depth_fbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, depth_color, 0);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, depth, 0);
        gl.viewport(0, 0, 32, 32);
        gl.clearDepth(1);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LESS);
        draw_flat(1, 1, 1, 0.5);
        gl.disable(gl.DEPTH_TEST);
        label("depth-initial", show_depth(depth));

        gl.bindFramebuffer(gl.FRAMEBUFFER, depth_fbo);
        gl.viewport(0, 0, 32, 32);
        gl.enable(gl.SCISSOR_TEST);
        gl.scissor(0, 0, 16, 32);
        gl.clearDepth(0);
        gl.clear(gl.DEPTH_BUFFER_BIT);
        gl.disable(gl.SCISSOR_TEST);
        label("depth-half", show_depth(depth));

        gl.bindFramebuffer(gl.FRAMEBUFFER, depth_fbo);
        gl.enable(gl.SCISSOR_TEST);
        gl.scissor(0, 0, 32, 32);
        gl.clearDepth(1);
        gl.clear(gl.DEPTH_BUFFER_BIT);
        gl.disable(gl.SCISSOR_TEST);
        label("depth-full", show_depth(depth));
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 12000 },
    );

    expect(value("programs")).toBe("true");
    expect(value("scissor-inside")).toBe("0,0,255,255");
    expect(value("scissor-outside")).toBe("255,0,0,255");
    expect(value("fbo")).toBe("true");
    expect(value("blit")).toBe("0,0,255,255");
    // Depth is sampled back through an RGBA8 target: 0.5 arrives as 127 or 128
    // depending on the backend's float -> UNORM rounding, so compare with a
    // tolerance rather than an exact byte value.
    near(value("depth-initial"), [128, 128, 128, 128], 2);
    near(value("depth-half"), [0, 128, 128, 128], 2);
    near(value("depth-full"), [255, 255, 255, 255], 2);
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });

  it("preserves pixels outside a partial framebuffer blit", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-partial-blit",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "partial-blit", width: 320, height: 240 });
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

        // The whole canvas is red; the blit source is blue.
        gl.viewport(0, 0, 64, 64);
        gl.clearColor(1, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);

        const target = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, target);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 32, 32, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        const fbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, target, 0);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(0, 0, 1, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);

        // Only the bottom-left quarter of the canvas: everywhere else has to
        // keep the red it had. A blit whose destination load-op discards the
        // attachment (Metal's MTLLoadActionDontCare) leaves those pixels
        // undefined, which is what this probe catches.
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, fbo);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        gl.blitFramebuffer(0, 0, 32, 32, 0, 0, 16, 16, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        label("blit-inside", probe(8, 8));
        label("blit-outside", probe(48, 48));
        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(value("blit-inside")).toBe("0,0,255,255");
    expect(value("blit-outside")).toBe("255,0,0,255");
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });
});
