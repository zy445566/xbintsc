/**
 * End-to-end tests for WebGL 2 render-target orientation.
 *
 * WebGL's framebuffer origin is bottom-left, and a texture attached to a
 * framebuffer must sample with `v = 0` at that same bottom row. SDL_GPU stores
 * the composited drawing buffer top-down (the page presents its first row at the
 * top), so a user framebuffer has to be stored bottom-up instead; `viewport`,
 * `scissor`, `readPixels`, `blitFramebuffer` and the internal resolve/blit quads
 * all map GL rows through the target's convention. Before that was the case a
 * render-to-texture pass sampled back upside down.
 *
 * Every probe draws content that is asymmetric in y, samples or blits it, and
 * reads the result back through `readPixels` (whose y is GL-ordered).
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><head><title>webgl2-target-orientation</title></head>
<body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL 2 render-target orientation", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("samples a user framebuffer with WebGL's bottom-left origin", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl2-target-orientation",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "target-orientation", width: 320, height: 240 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);

      win.on("ready", () => {
        const canvas: any = document.getElementById("c");
        // blitFramebuffer cannot target a multisampled draw framebuffer
        // (ES 3.0 4.3.2), so the canvas is created without antialiasing.
        const gl: any = canvas.getContext("webgl2", { antialias: false });
        if (gl == null) {
          console.log("webgl2=false");
          win.close();
          return;
        }

        const px = new Uint8Array(4);
        const probe = (x: number, y: number): string => {
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };
        const readAt = (fb: any, x: number, y: number): string => {
          gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
          gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
          return px[0] + "," + px[1] + "," + px[2] + "," + px[3];
        };

        const program = (vsSource: string, fsSource: string): any => {
          const vs: any = gl.createShader(gl.VERTEX_SHADER);
          gl.shaderSource(vs, vsSource);
          gl.compileShader(vs);
          const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
          gl.shaderSource(fs, fsSource);
          gl.compileShader(fs);
          const linked: any = gl.createProgram();
          gl.attachShader(linked, vs);
          gl.attachShader(linked, fs);
          gl.linkProgram(linked);
          return linked;
        };
        const flat = program(
          "#version 300 es\\nin vec2 a_pos; uniform vec4 u_rect;" +
          "void main() { gl_Position = vec4(mix(u_rect.xy, u_rect.zw, a_pos), 0.0, 1.0); }",
          "#version 300 es\\nprecision highp float; uniform vec4 u_color;" +
          "out vec4 fragColor; void main() { fragColor = u_color; }");
        const sample = program(
          "#version 300 es\\nin vec2 a_pos; out vec2 v_uv;" +
          "void main() { v_uv = a_pos;" +
          "  gl_Position = vec4(a_pos * 2.0 - 1.0, 0.0, 1.0); }",
          "#version 300 es\\nprecision highp float; in vec2 v_uv;" +
          "uniform sampler2D u_tex; out vec4 fragColor;" +
          "void main() { fragColor = texture(u_tex, v_uv); }");
        label("programs", gl.getProgramParameter(flat, gl.LINK_STATUS) === true &&
          gl.getProgramParameter(sample, gl.LINK_STATUS) === true);

        const quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1]),
          gl.STATIC_DRAW);
        const bindQuad = (linked: any): void => {
          gl.bindBuffer(gl.ARRAY_BUFFER, quad);
          const pos: any = gl.getAttribLocation(linked, "a_pos");
          gl.enableVertexAttribArray(pos);
          gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 8, 0);
        };
        // Fill an NDC rectangle. NDC y = +1 is the GL top of the framebuffer.
        const fill = (rect: number[], color: number[]): void => {
          gl.useProgram(flat);
          bindQuad(flat);
          gl.uniform4f(gl.getUniformLocation(flat, "u_rect"), rect[0], rect[1], rect[2], rect[3]);
          gl.uniform4f(gl.getUniformLocation(flat, "u_color"), color[0], color[1], color[2], 1);
          gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
        };
        // Draw the texture over the whole 64x64 canvas: canvas GL y sees v = y / 64.
        const show = (texture: any): void => {
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.viewport(0, 0, 64, 64);
          gl.useProgram(sample);
          bindQuad(sample);
          gl.uniform1i(gl.getUniformLocation(sample, "u_tex"), 0);
          gl.bindTexture(gl.TEXTURE_2D, texture);
          gl.clearColor(0, 0, 0, 1);
          gl.clear(gl.COLOR_BUFFER_BIT);
          gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
        };

        // --- a 32x32 target: GL top half red, bottom half green -------------
        const target = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, target);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 32, 32, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        const fbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, target, 0);
        label("complete", gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(0, 1, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        fill([-1, 0, 1, 1], [1, 0, 0, 1]);
        // readPixels on a user framebuffer is GL-ordered too: y = 0 is its bottom.
        label("fb-bottom", readAt(fbo, 16, 8));
        label("fb-top", readAt(fbo, 16, 24));

        // Sampling must agree: the canvas's upper half shows the target's top.
        show(target);
        label("sampled-top", probe(32, 48));
        label("sampled-bottom", probe(32, 16));

        // --- blit from the target to the top-down canvas --------------------
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, 64, 64);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, fbo);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        gl.blitFramebuffer(0, 0, 32, 32, 0, 0, 64, 64, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        label("blit-error", gl.getError());
        label("blit-top", probe(32, 48));
        label("blit-bottom", probe(32, 16));

        // --- a partial blit flips the rows of just its rectangle -------------
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, 64, 64);
        gl.clearColor(0, 0, 1, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, fbo);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        // The target's GL top-left 16x16 quarter (red) goes to the canvas centre.
        gl.blitFramebuffer(0, 16, 16, 32, 16, 16, 32, 32, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        label("partial-in", probe(24, 24));
        label("partial-out", probe(48, 48));

        // --- blit from the canvas back into the bottom-up target -------------
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        // The canvas's red block sits at GL (16..32, 16..32); a 1:1 copy keeps
        // it at the target's GL bottom-left quarter.
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, fbo);
        gl.blitFramebuffer(16, 16, 32, 32, 0, 0, 16, 16, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        label("canvas-to-fb", readAt(fbo, 4, 4) + "/" + readAt(fbo, 4, 24));

        // --- a scissored clear targets GL rows on a user framebuffer ---------
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.enable(gl.SCISSOR_TEST);
        gl.scissor(0, 24, 32, 8);  // GL top strip
        gl.clearColor(0, 1, 1, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.disable(gl.SCISSOR_TEST);
        label("scissor-top", readAt(fbo, 16, 26));
        label("scissor-bottom", readAt(fbo, 16, 8));
        show(target);
        label("scissor-sampled-top", probe(32, 48));
        label("scissor-sampled-bottom", probe(32, 16));

        // --- an antialiased user framebuffer clears by GL rows too ----------
        label("max-samples", gl.getParameter(gl.MAX_SAMPLES));
        const msaaColor = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, msaaColor);
        gl.renderbufferStorageMultisample(gl.RENDERBUFFER, 4, gl.RGBA8, 32, 32);
        const msaaFbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, msaaFbo);
        gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER,
          msaaColor);
        label("msaa-complete", gl.checkFramebufferStatus(gl.FRAMEBUFFER) ===
          gl.FRAMEBUFFER_COMPLETE);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.enable(gl.SCISSOR_TEST);
        gl.scissor(0, 24, 32, 8);  // GL top strip
        gl.clearColor(0, 1, 1, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.disable(gl.SCISSOR_TEST);
        // Resolve it into a single-sampled texture and sample that on the canvas.
        const resolved = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, resolved);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 32, 32, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        const resolveFbo = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, resolveFbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, resolved, 0);
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, msaaFbo);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, resolveFbo);
        gl.blitFramebuffer(0, 0, 32, 32, 0, 0, 32, 32, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        show(resolved);
        label("msaa-sampled-top", probe(32, 48));
        label("msaa-sampled-bottom", probe(32, 16));

        // --- face culling still follows GL's winding on the target -----------
        // The viewport flip also mirrors the winding the rasteriser sees, so the
        // front-face comparison has to follow the target's convention.
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.viewport(0, 0, 32, 32);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        const tri = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, tri);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0, 1, 0, 0, 1]), gl.STATIC_DRAW);
        gl.useProgram(flat);
        const tri_pos: any = gl.getAttribLocation(flat, "a_pos");
        gl.enableVertexAttribArray(tri_pos);
        gl.vertexAttribPointer(tri_pos, 2, gl.FLOAT, false, 8, 0);
        gl.uniform4f(gl.getUniformLocation(flat, "u_rect"), -1, -1, 1, 1);
        gl.uniform4f(gl.getUniformLocation(flat, "u_color"), 1, 0, 0, 1);
        gl.enable(gl.CULL_FACE);
        gl.cullFace(gl.BACK);
        gl.frontFace(gl.CCW);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("cull-ccw", readAt(fbo, 6, 6));
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.frontFace(gl.CW);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        label("cull-cw", readAt(fbo, 6, 6));
        gl.disable(gl.CULL_FACE);
        gl.bindBuffer(gl.ARRAY_BUFFER, quad);

        label("error", gl.getError());
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, autocloseMs: 15000 },
    );

    expect(value("programs")).toBe("true");
    expect(value("complete")).toBe("true");
    expect(value("fb-bottom")).toBe("0,255,0,255");
    expect(value("fb-top")).toBe("255,0,0,255");
    expect(value("sampled-top")).toBe("255,0,0,255");
    expect(value("sampled-bottom")).toBe("0,255,0,255");
    expect(value("blit-error")).toBe("0");
    expect(value("blit-top")).toBe("255,0,0,255");
    expect(value("blit-bottom")).toBe("0,255,0,255");
    expect(value("cull-ccw")).toBe("255,0,0,255");
    expect(value("cull-cw")).toBe("0,0,0,255");
    expect(value("partial-in")).toBe("255,0,0,255");
    expect(value("partial-out")).toBe("0,0,255,255");
    expect(value("canvas-to-fb")).toBe("255,0,0,255/0,0,0,255");
    expect(value("scissor-top")).toBe("0,255,255,255");
    expect(value("scissor-bottom")).toBe("0,0,0,255");
    expect(value("scissor-sampled-top")).toBe("0,255,255,255");
    expect(value("scissor-sampled-bottom")).toBe("0,0,0,255");
    if (Number(value("max-samples")) > 0) {
      expect(value("msaa-complete")).toBe("true");
      expect(value("msaa-sampled-top")).toBe("0,255,255,255");
      expect(value("msaa-sampled-bottom")).toBe("0,0,0,255");
    }
    expect(value("error")).toBe("0");
    expect(stderr).not.toContain("pipeline creation failed");
  });
});
