/**
 * GUI e2e: WebGL 2 identity and three.js-style sampler arrays.
 *
 * A browser reports a WebGL 2 context as `WebGL2RenderingContext` (and *not* as
 * `WebGLRenderingContext`), which is how three.js decides whether to use its
 * WebGL 2 path. Its shadow-map chunks also index sampler arrays with a loop
 * variable; Vulkan GLSL only accepts a constant index, so the rewriter has to
 * expand the pragma-marked loop before compiling.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl2-shadow</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
  <canvas id="c1" width="32" height="32"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL 2 identity and sampler arrays", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("reports the WebGL 2 constructor/prototype and unrolls shadow loops", () => {
    const { value } = harness.compileAndRun(
      "webgl2-shadow",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "webgl2-shadow", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2");
        label("webgl2", gl != null);
        if (gl == null) {
          win.close();
          return;
        }
        label("constructor", gl.constructor.name);
        label("instanceof-webgl2", gl instanceof WebGL2RenderingContext);
        label("instanceof-webgl1", gl instanceof WebGLRenderingContext);
        label("canvas", gl.canvas === canvas);
        label("drawing-buffer", gl.drawingBufferWidth + "x" + gl.drawingBufferHeight);
        label("shared-prototype",
          gl.constructor.prototype === WebGLRenderingContext.prototype);

        const canvas1: any = document.getElementById("c1");
        const gl1: any = canvas1.getContext("webgl");
        label("webgl1", gl1 != null);
        if (gl1 != null) {
          label("constructor1", gl1.constructor.name);
          label("gl1-instanceof-webgl2", gl1 instanceof WebGL2RenderingContext);
          label("gl1-instanceof-webgl1", gl1 instanceof WebGLRenderingContext);
        }

        const vs: any = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, "#version 300 es\\n" +
          "in vec2 a_pos; void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }");
        gl.compileShader(vs);
        label("vs", gl.getShaderParameter(vs, gl.COMPILE_STATUS));

        const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, "#version 300 es\\n" +
          "precision highp float;\\n" +
          "uniform sampler2D u_shadowMap[2];\\n" +
          "out highp vec4 fragColor;\\n" +
          "void main() {\\n" +
          "  float shadow = 0.0;\\n" +
          "  #pragma unroll_loop_start\\n" +
          "  for ( int i = 0; i < 2; i ++ ) {\\n" +
          "    #if ( UNROLLED_LOOP_INDEX < 2 )\\n" +
          "    shadow += texture( u_shadowMap[ i ], vec2( 0.5 ) ).r;\\n" +
          "    #endif\\n" +
          "  }\\n" +
          "  #pragma unroll_loop_end\\n" +
          "  fragColor = vec4( shadow );\\n" +
          "}");
        gl.compileShader(fs);
        const ok = gl.getShaderParameter(fs, gl.COMPILE_STATUS);
        label("fs", ok === true ? "true" : "false:" + gl.getShaderInfoLog(fs));

        const program: any = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        label("link", gl.getProgramParameter(program, gl.LINK_STATUS));
        win.close();
      });
      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE } },
    );

    expect(value("webgl2")).toBe("true");
    expect(value("constructor")).toBe("WebGL2RenderingContext");
    expect(value("instanceof-webgl2")).toBe("true");
    expect(value("instanceof-webgl1")).toBe("false");
    expect(value("shared-prototype")).toBe("false");
    expect(value("canvas")).toBe("true");
    expect(value("drawing-buffer")).toBe("64x64");
    expect(value("webgl1")).toBe("true");
    expect(value("constructor1")).toBe("WebGLRenderingContext");
    expect(value("gl1-instanceof-webgl2")).toBe("false");
    expect(value("gl1-instanceof-webgl1")).toBe("true");
    expect(value("vs")).toBe("true");
    expect(value("fs")).toBe("true");
    expect(value("link")).toBe("true");
  });
});
