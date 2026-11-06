// The WebGL 2 backend: shader programs, VAOs, static meshes, instanced scenery
// and the pixel probes. No three.js, no textures, no helper library — just the
// WebGL 2 API and the shader sources in shaders.js.
//
// `renderer.js` picks between this backend and the WebGPU one in
// renderer-webgpu.js; both expose the same small drawing interface so the
// scene code in main.js never branches on the backend.

import {
  LIT_FRAGMENT,
  LIT_VERTEX,
  INSTANCED_FRAGMENT,
  INSTANCED_VERTEX,
  SHADOW_FRAGMENT,
  SHADOW_VERTEX,
  SKY_FRAGMENT,
  SKY_VERTEX,
} from "./shaders.js";
import { box, cylinder, discXZ, sphere } from "./geometry.js";

const LIT_UNIFORMS = [
  "u_view_proj", "u_model", "u_normal_scale", "u_light_dir", "u_camera",
  "u_fog_color", "u_fog_density", "u_tint", "u_alpha",
];
const SKY_UNIFORMS = ["u_resolution", "u_time"];
const INSTANCED_UNIFORMS = [
  "u_view_proj", "u_scroll", "u_range", "u_light_dir", "u_camera",
  "u_fog_color", "u_fog_density",
];
const SHADOW_UNIFORMS = ["u_view_proj", "u_model", "u_alpha"];

/** Unit geometry has no baked colour; the per-draw tint supplies it. */
function whiteColors(vertexCount) {
  const colors = new Float32Array(vertexCount * 3);
  for (let i = 0; i < colors.length; i += 1) colors[i] = 1;
  return colors;
}

function compile(gl, type, source) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    const log = gl.getShaderInfoLog(shader);
    gl.deleteShader(shader);
    throw new Error("shader compile failed: " + log);
  }
  return shader;
}

function program(gl, vertexSource, fragmentSource, names) {
  const vertex = compile(gl, gl.VERTEX_SHADER, vertexSource);
  const fragment = compile(gl, gl.FRAGMENT_SHADER, fragmentSource);
  const handle = gl.createProgram();
  gl.attachShader(handle, vertex);
  gl.attachShader(handle, fragment);
  gl.linkProgram(handle);
  gl.deleteShader(vertex);
  gl.deleteShader(fragment);
  if (!gl.getProgramParameter(handle, gl.LINK_STATUS)) {
    const log = gl.getProgramInfoLog(handle);
    gl.deleteProgram(handle);
    throw new Error("program link failed: " + log);
  }
  const uniforms = {};
  for (let i = 0; i < names.length; i += 1) {
    uniforms[names[i]] = gl.getUniformLocation(handle, names[i]);
  }
  return { handle: handle, uniforms: uniforms };
}

function uniform3f(gl, location, x, y, z) {
  if (location !== null && location !== undefined) gl.uniform3f(location, x, y, z);
}

function uniform1f(gl, location, value) {
  if (location !== null && location !== undefined) gl.uniform1f(location, value);
}

function uniformMatrix(gl, location, value) {
  if (location !== null && location !== undefined) gl.uniformMatrix4fv(location, false, value);
}

export class WebGLRenderer {
  constructor(gl) {
    this.gl = gl;
    this.kind = "webgl2";
    this.label = "WEBGL 2";
    this.glVersion = String(gl.getParameter(gl.VERSION));
    this.glslVersion = String(gl.getParameter(gl.SHADING_LANGUAGE_VERSION));
    const driverName = String(gl.getParameter(gl.RENDERER)).toUpperCase();
    this.gpuLabel = driverName.indexOf("VULKAN") >= 0
      ? "VULKAN"
      : driverName.indexOf("METAL") >= 0
        ? "METAL"
        : "SDL_GPU";
    this.width = 0;
    this.height = 0;
    this.lit = program(gl, LIT_VERTEX, LIT_FRAGMENT, LIT_UNIFORMS);
    this.sky = program(gl, SKY_VERTEX, SKY_FRAGMENT, SKY_UNIFORMS);
    this.instanced = program(gl, INSTANCED_VERTEX, INSTANCED_FRAGMENT, INSTANCED_UNIFORMS);
    this.shadow = program(gl, SHADOW_VERTEX, SHADOW_FRAGMENT, SHADOW_UNIFORMS);
    this.stats = { draws: 0, triangles: 0 };
    this.skyMesh = this.createSkyMesh();
    this.unitCylinder = this.createMesh(cylinder(8));
    this.unitSphere = this.createMesh(sphere(10, 14));
    this.unitBox = this.createMesh(box());
  }

  createMesh(geometry) {
    const gl = this.gl;
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);

    const position = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, position);
    gl.bufferData(gl.ARRAY_BUFFER, geometry.positions, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);

    const normal = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, normal);
    gl.bufferData(gl.ARRAY_BUFFER, geometry.normals, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);

    const colors = geometry.colors === undefined ? whiteColors(geometry.positions.length / 3) : geometry.colors;
    const color = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, color);
    gl.bufferData(gl.ARRAY_BUFFER, colors, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 3, gl.FLOAT, false, 0, 0);

    const index = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, index);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, geometry.indices, gl.STATIC_DRAW);

    gl.bindVertexArray(null);

    return {
      vao: vao,
      indexCount: geometry.indices.length,
      indexType: geometry.indices instanceof Uint32Array ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT,
    };
  }

  createInstancedMesh(geometry, instances) {
    const gl = this.gl;
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);

    const position = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, position);
    gl.bufferData(gl.ARRAY_BUFFER, geometry.positions, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);

    const normal = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, normal);
    gl.bufferData(gl.ARRAY_BUFFER, geometry.normals, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);

    const color = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, color);
    gl.bufferData(gl.ARRAY_BUFFER, geometry.colors, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 3, gl.FLOAT, false, 0, 0);

    const instance = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, instance);
    gl.bufferData(gl.ARRAY_BUFFER, instances, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(3);
    gl.vertexAttribPointer(3, 4, gl.FLOAT, false, 0, 0);
    gl.vertexAttribDivisor(3, 1);

    const index = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, index);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, geometry.indices, gl.STATIC_DRAW);

    gl.bindVertexArray(null);

    return {
      vao: vao,
      indexCount: geometry.indices.length,
      indexType: geometry.indices instanceof Uint32Array ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT,
      instances: instances.length / 4,
    };
  }

  createSkyMesh() {
    const gl = this.gl;
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const buffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(
      gl.ARRAY_BUFFER,
      new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]),
      gl.STATIC_DRAW,
    );
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    return { vao: vao };
  }

  begin(width, height, viewProj, camera, lightDir, fogColor, fogDensity) {
    const gl = this.gl;
    this.width = width;
    this.height = height;
    this.stats.draws = 0;
    this.stats.triangles = 0;
    gl.viewport(0, 0, width, height);
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.depthMask(true);
    gl.disable(gl.CULL_FACE);
    gl.disable(gl.BLEND);
    gl.clearColor(fogColor[0], fogColor[1], fogColor[2], 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

    gl.useProgram(this.lit.handle);
    uniformMatrix(gl, this.lit.uniforms.u_view_proj, viewProj);
    uniform3f(gl, this.lit.uniforms.u_light_dir, lightDir[0], lightDir[1], lightDir[2]);
    uniform3f(gl, this.lit.uniforms.u_camera, camera[0], camera[1], camera[2]);
    uniform3f(gl, this.lit.uniforms.u_fog_color, fogColor[0], fogColor[1], fogColor[2]);
    uniform1f(gl, this.lit.uniforms.u_fog_density, fogDensity);

    gl.useProgram(this.instanced.handle);
    uniformMatrix(gl, this.instanced.uniforms.u_view_proj, viewProj);
    uniform3f(gl, this.instanced.uniforms.u_light_dir, lightDir[0], lightDir[1], lightDir[2]);
    uniform3f(gl, this.instanced.uniforms.u_camera, camera[0], camera[1], camera[2]);
    uniform3f(gl, this.instanced.uniforms.u_fog_color, fogColor[0], fogColor[1], fogColor[2]);
    uniform1f(gl, this.instanced.uniforms.u_fog_density, fogDensity);

    gl.useProgram(this.shadow.handle);
    uniformMatrix(gl, this.shadow.uniforms.u_view_proj, viewProj);
  }

  /** The whole frame is recorded in `begin`; WebGL needs no commit step. */
  end() {}

  drawSky(width, height, time) {
    const gl = this.gl;
    gl.useProgram(this.sky.handle);
    gl.uniform2f(this.sky.uniforms.u_resolution, width, height);
    uniform1f(gl, this.sky.uniforms.u_time, time);
    gl.disable(gl.DEPTH_TEST);
    gl.depthMask(false);
    gl.bindVertexArray(this.skyMesh.vao);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    gl.bindVertexArray(null);
    gl.depthMask(true);
    gl.enable(gl.DEPTH_TEST);
    this.stats.draws += 1;
    this.stats.triangles += 2;
  }

  draw(mesh, model, normalScale, tint, alpha) {
    const gl = this.gl;
    gl.useProgram(this.lit.handle);
    uniformMatrix(gl, this.lit.uniforms.u_model, model);
    const ns = normalScale === undefined ? [1, 1, 1] : normalScale;
    const c = tint === undefined ? [1, 1, 1] : tint;
    const a = alpha === undefined ? 1 : alpha;
    uniform3f(gl, this.lit.uniforms.u_normal_scale, ns[0], ns[1], ns[2]);
    uniform3f(gl, this.lit.uniforms.u_tint, c[0], c[1], c[2]);
    uniform1f(gl, this.lit.uniforms.u_alpha, a);
    gl.bindVertexArray(mesh.vao);
    gl.drawElements(gl.TRIANGLES, mesh.indexCount, mesh.indexType, 0);
    gl.bindVertexArray(null);
    this.stats.draws += 1;
    this.stats.triangles += mesh.indexCount / 3;
  }

  drawInstanced(mesh, scroll, range) {
    const gl = this.gl;
    gl.useProgram(this.instanced.handle);
    uniform1f(gl, this.instanced.uniforms.u_scroll, scroll);
    uniform1f(gl, this.instanced.uniforms.u_range, range);
    gl.bindVertexArray(mesh.vao);
    gl.drawElementsInstanced(gl.TRIANGLES, mesh.indexCount, mesh.indexType, 0, mesh.instances);
    gl.bindVertexArray(null);
    this.stats.draws += 1;
    this.stats.triangles += (mesh.indexCount / 3) * mesh.instances;
  }

  drawShadow(mesh, model, alpha) {
    const gl = this.gl;
    gl.useProgram(this.shadow.handle);
    uniformMatrix(gl, this.shadow.uniforms.u_model, model);
    uniform1f(gl, this.shadow.uniforms.u_alpha, alpha);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.depthMask(false);
    gl.bindVertexArray(mesh.vao);
    gl.drawElements(gl.TRIANGLES, mesh.indexCount, mesh.indexType, 0);
    gl.bindVertexArray(null);
    gl.depthMask(true);
    gl.disable(gl.BLEND);
    this.stats.draws += 1;
    this.stats.triangles += mesh.indexCount / 3;
  }

  /** Sample one pixel, `y` measured up from the bottom (readPixels order). */
  probe(x, y) {
    const gl = this.gl;
    const pixel = new Uint8Array(4);
    gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
    return pixel[0] + "," + pixel[1] + "," + pixel[2] + "," + pixel[3];
  }

  /** Read the whole colour buffer, bottom row first (readPixels order). */
  snapshot() {
    const gl = this.gl;
    const pixels = new Uint8Array(this.width * this.height * 4);
    gl.readPixels(0, 0, this.width, this.height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
    return pixels;
  }

  probeMax(x, y, width, height) {
    const gl = this.gl;
    const pixels = new Uint8Array(width * height * 4);
    gl.readPixels(x, y, width, height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
    let best = 0;
    let index = 0;
    for (let i = 0; i < width * height; i += 1) {
      const sum = pixels[i * 4] + pixels[i * 4 + 1] + pixels[i * 4 + 2];
      if (sum > best) {
        best = sum;
        index = i;
      }
    }
    return pixels[index * 4] + "," + pixels[index * 4 + 1] + "," + pixels[index * 4 + 2] + ",255";
  }
}

/** A unit disc mesh used for contact shadows. */
export function createShadowDisc() {
  return discXZ(36);
}
