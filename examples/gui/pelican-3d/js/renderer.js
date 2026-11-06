// Backend selection for Pelican Ride 3D.
//
// `createRenderer` returns a WebGL 2 or a WebGPU renderer; both implement the
// same drawing interface (createMesh / createInstancedMesh / begin / drawSky /
// draw / drawInstanced / drawShadow / end) plus the pixel-probe helpers used by
// the native host report. `drawTube` is shared by the two.

import { WebGLRenderer } from "./renderer-webgl.js";
import { WebGPURenderer } from "./renderer-webgpu.js";
import { alignY, multiply, scaling, translation } from "./math.js";

export { WebGLRenderer, WebGPURenderer };

/** Create a renderer for `backend` ("webgl2" or "webgpu"). */
export async function createRenderer(backend, canvas, width, height) {
  if (backend === "webgpu") {
    return WebGPURenderer.create(canvas, width, height);
  }
  const gl = canvas.getContext("webgl2", {
    alpha: false,
    antialias: true,
    depth: true,
    stencil: false,
  });
  if (gl === null || gl === undefined) {
    throw new Error("WebGL 2 is not available on this GPU backend");
  }
  return new WebGLRenderer(gl);
}

/** Convenience: draw a thin cylinder between two points with a tint. */
export function drawTube(renderer, mesh, a, b, radius, tint) {
  const dx = b[0] - a[0];
  const dy = b[1] - a[1];
  const dz = b[2] - a[2];
  const length = Math.sqrt(dx * dx + dy * dy + dz * dz);
  if (!(length > 0.00001)) return;
  const align = alignY([dx / length, dy / length, dz / length]);
  const model = multiply(
    translation((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2),
    multiply(align, scaling(radius, length, radius)),
  );
  renderer.draw(mesh, model, [1 / radius, 1 / length, 1 / radius], tint, 1);
}
