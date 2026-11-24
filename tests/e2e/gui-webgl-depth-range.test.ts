/**
 * End-to-end test for `gl.depthRange`, the state a first-person renderer uses to
 * keep its view model (weapon, hands) in front of the world.
 *
 * three.js-driven games render the world with one camera, then narrow the depth
 * range (`depthRange(0, 0.05)`) and render the view model with a second camera
 * that has a much smaller near plane. Every view-model fragment then lands in
 * the first 5% of the depth buffer, so the world can never occlude it — without
 * that remap the hand is depth-tested at its real distance and the ground cuts
 * through it (saltline's "the hand sinks into the floor" bug).
 *
 * The probes below pin the observable halves of the GL semantics: the default
 * [0, 1] range leaves the ordering alone, a narrowed range pulls farther
 * fragments in front, the range also rewrites the depth a fragment *writes*
 * (not just the value it tests against), and `getParameter(DEPTH_RANGE)`
 * reports the live range.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl-depth-range</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL depth range", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("remaps fragment depth and depth writes into the requested range", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl-depth-range",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "depth-range", width: 320, height: 240 });
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
        gl.shaderSource(vs,
          "#version 300 es\\n" +
          "in vec2 a_pos;\\n" +
          "uniform float u_z;\\n" +
          "void main() { gl_Position = vec4(a_pos, u_z, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs,
          "#version 300 es\\n" +
          "precision highp float;\\n" +
          "uniform vec4 u_color;\\n" +
          "out vec4 fragColor;\\n" +
          "void main() { fragColor = u_color; }");
        gl.compileShader(fs);
        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        label("link", gl.getProgramParameter(program, gl.LINK_STATUS) === true);

        const quad: any = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
        gl.useProgram(program);
        const pos: any = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        const z_loc: any = gl.getUniformLocation(program, "u_z");
        const color_loc: any = gl.getUniformLocation(program, "u_color");

        const px = new Uint8Array(4);
        const probe = (): string => {
          gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };
        const draw = (z: number, r: number, g: number, b: number): void => {
          gl.uniform1f(z_loc, z);
          gl.uniform4f(color_loc, r, g, b, 1);
          gl.drawArrays(gl.TRIANGLES, 0, 3);
        };
        const reset = (): void => {
          gl.clearColor(0, 0, 0, 1);
          gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        };

        gl.viewport(0, 0, 64, 64);
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LESS);
        gl.depthMask(true);

        /* -- 1. default range: a farther fragment is rejected ---------------- */
        gl.depthRange(0, 1);
        reset();
        draw(0.8, 1, 0, 0);
        draw(0.96, 0, 1, 0);
        label("default", probe());

        /* -- 2. narrowed range: every fragment lands in front ---------------- */
        reset();
        draw(0.8, 1, 0, 0);
        gl.depthRange(0, 0.05);
        draw(0.96, 0, 1, 0);
        gl.depthRange(0, 1);
        label("compress", probe());

        /* -- 3. narrowed range also remaps the depth that is written --------- */
        reset();
        gl.depthRange(0, 0.1);
        draw(0.96, 1, 0, 0);
        gl.depthRange(0, 1);
        draw(0.8, 0, 1, 0);
        label("write", probe());

        gl.depthRange(0.25, 0.75);
        const range: any = gl.getParameter(gl.DEPTH_RANGE);
        label("range-param", range[0] + "," + range[1]);
        gl.depthRange(0, 1);

        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 10000 },
    );

    expect(value("link")).toBe("true");
    expect(value("default")).toBe("255,0,0,255");
    expect(value("compress")).toBe("0,255,0,255");
    expect(value("write")).toBe("255,0,0,255");
    expect(value("range-param")).toBe("0.25,0.75");
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });
});
