/**
 * End-to-end test for the `gui` extension — WebGL pipeline cache reuse.
 *
 * A pipeline is a function of the program, the vertex *layout* and the
 * fixed-function state. It is not a function of which buffer currently holds
 * the data, nor of the monotonic counters the engine bumps when a VAO is bound
 * or a framebuffer is switched. Keying the pipeline cache on those counters
 * made every draw create a fresh `SDL_GPUGraphicsPipeline` once a frame bound
 * more than one layout (three.js binds one VAO per geometry): a measured
 * ~250 pipeline creations per frame, which cost more than the whole rest of
 * the frame.
 *
 * This program draws two triangles whose layouts are identical and whose
 * buffers differ, alternating the bound buffer every frame. The layout never
 * changes, so after the first frame the pipeline must come from the cache:
 * `webglPipelineMisses` may not grow. Before the fix it grew by two per frame.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl pipeline cache</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL pipeline cache", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("reuses the pipeline when only the bound buffers change", () => {
    const { value } = harness.compileAndRun(
      "webgl-pipeline-cache",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "pipeline cache", width: 320, height: 240 });
      win.loadHTML(page);
      const out = (name: string, value: any): void => console.log(name + "=" + value);

      let gl: any = null;
      let position = 0;
      let buffers: any[] = [];

      /* A triangle in its own buffer: same layout, different data. */
      const makeBuffer = (center: number): any => {
        const buffer: any = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
          center - 0.4, -0.4, center + 0.4, -0.4, center, 0.5,
        ]), gl.STATIC_DRAW);
        return buffer;
      };

      let frame = 0;
      let warm: any = null;

      const tick = (): void => {
        frame = frame + 1;
        for (const buffer of buffers) {
          gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
          gl.vertexAttribPointer(position, 2, gl.FLOAT, false, 0, 0);
          gl.drawArrays(gl.TRIANGLES, 0, 3);
        }
        /* Frames 1-3 warm the cache (shader translation, the first pipelines and
         * the one-time GPU setup). From there the layout never changes. */
        if (frame === 3) warm = win.stats();
        if (frame < 8) {
          win.requestAnimationFrame(tick);
          return;
        }
        const now: any = win.stats();
        out("misses.warm", now.webglPipelineMisses - warm.webglPipelineMisses);
        out("draws.warm", now.webglDraws - warm.webglDraws);
        out("misses.total", now.webglPipelineMisses);
        out("frames", frame);
        win.close();
      };

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        gl = canvas.getContext("webgl");
        out("webgl", gl != null);
        if (gl == null) {
          win.close();
          return;
        }

        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "attribute vec2 a_pos;" +
          "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "precision mediump float;" +
          "void main() { gl_FragColor = vec4(0.2, 0.6, 1.0, 1.0); }");
        gl.compileShader(fs);

        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        out("link", gl.getProgramParameter(program, gl.LINK_STATUS));
        gl.useProgram(program);

        position = gl.getAttribLocation(program, "a_pos");
        gl.enableVertexAttribArray(position);
        buffers = [makeBuffer(-0.4), makeBuffer(0.4)];
        win.requestAnimationFrame(tick);
      });
      win.on("close", () => console.log("closed"));
      `,
      { files: { "page.html": PAGE }, autocloseMs: 15000, console: true },
    );

    expect(value("webgl")).toBe("true");
    expect(value("link")).toBe("true");
    expect(value("frames")).toBe("8");
    /* The draw calls really happened. */
    expect(Number(value("draws.warm"))).toBeGreaterThan(0);
    /* ... and none of them built a pipeline: the layout is unchanged. */
    expect(Number(value("misses.warm"))).toBe(0);
    /* The cache stays bounded: a handful for the whole program. */
    expect(Number(value("misses.total"))).toBeLessThan(8);
  });
});
