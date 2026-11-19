/**
 * End-to-end test for WebGL texture state following the *active* texture unit.
 *
 * `texImage2D`, `texParameteri` and `generateMipmap` mutate the binding of the
 * active unit, so a page that binds a texture on unit 1 and allocates it there
 * must not touch the texture bound on unit 0. The engine used to route every
 * texture-state call to unit 0, which silently left render-target textures
 * without storage and corrupted unrelated textures — a three.js page that
 * resizes a render target while another texture is active ended up with
 * incomplete framebuffers and a blank (white) viewport.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const PAGE = `
<html><body style="margin:0">
  <canvas id="c" width="64" height="64"></canvas>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — WebGL active texture unit", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("keeps texture storage and parameters on the active unit", () => {
    const { value, stderr } = harness.compileAndRun(
      "webgl-texture-units",
      `
      import { createWindow } from "gui";
      import page from "./page.html";

      const win = createWindow({ title: "texture-units", width: 320, height: 240 });

      win.on("ready", () => {
        const label = (name: string, value: any): void => console.log(name + "=" + value);
        const canvas: any = document.getElementById("c");
        const gl: any = canvas.getContext("webgl2", { antialias: false });
        if (gl == null) {
          label("context", "missing");
          win.close();
          return;
        }

        const red = new Uint8Array([255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255]);
        const green = new Uint8Array([0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255]);

        /* Unit 0: a 2x2 red texture. */
        const a: any = gl.createTexture();
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, a);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 2, 2, 0, gl.RGBA, gl.UNSIGNED_BYTE, red);

        /* Unit 1: a 2x2 green texture. Before the fix the upload landed on unit 0
         * (replacing A), because texture state ignored the active unit. */
        const b: any = gl.createTexture();
        gl.activeTexture(gl.TEXTURE1);
        gl.bindTexture(gl.TEXTURE_2D, b);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 2, 2, 0, gl.RGBA, gl.UNSIGNED_BYTE, green);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

        const readTexture = (name: string, texture: any): void => {
          const framebuffer: any = gl.createFramebuffer();
          gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
          gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
          label(name + "-complete", gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
          const pixels = new Uint8Array(4);
          gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
          label(name, pixels[0] + "," + pixels[1] + "," + pixels[2]);
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.deleteFramebuffer(framebuffer);
        };

        /* Each texture keeps its own storage: the unit-1 upload must not have
         * replaced the unit-0 texture, and both must be complete attachments. */
        readTexture("unit-0-texture", a);
        readTexture("unit-1-texture", b);
        win.close();
      });

      win.loadHTML(page);
      `,
      { files: { "page.html": PAGE }, console: true, autocloseMs: 20000 },
    );

    expect(stderr).not.toContain("error");
    /* A unit-1 upload must not have replaced unit 0's red texture. */
    expect(value("unit-0-texture")).toBe("255,0,0");
    expect(value("unit-0-texture-complete")).toBe("true");
    expect(value("unit-1-texture")).toBe("0,255,0");
    expect(value("unit-1-texture-complete")).toBe("true");
  });
});
