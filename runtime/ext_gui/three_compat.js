/*
 * xbintsc GUI — three.js `WebGLRenderer` compatibility layer.
 *
 * The GUI engine has no WebGL backend (doc/gui/known-issues.md). Three.js has
 * no Canvas2D fallback either, so a three.js page compiled with `--ext gui`
 * starts and then dies on "Error creating WebGL context". This file is appended
 * to the vendored three.js module by the GUI asset loader (see
 * src/extensions/gui/three.ts); it renames the original WebGL renderer and
 * defines a replacement with the same public surface that draws the scene with
 * the engine's Canvas2D rasteriser.
 *
 * It is a *scene* renderer, not a GL implementation: it understands the
 * primitives three.js uses here (spheres, capsules, cylinders, cones, boxes,
 * planes, sprites, points, instanced silhouettes), the standard/basic material
 * colours, the light types, fog and tone mapping. Curved primitives are drawn
 * as lit gradients, boxes/planes as shaded faces, and CanvasTextures are blitted
 * per plane cell so the floor/wall/sign keep their art. A triangle fallback
 * covers geometry the fast paths do not recognise.
 *
 * Everything here is ordinary JavaScript inside the three module's scope, so it
 * can use the module's own classes (Color, Vector3, Matrix4, constants) without
 * a second copy of the library.
 */

var __xtSwTAU = Math.PI * 2;

/* Tone mapping / output colour. Set once per frame by the renderer. */
var __xtSwExposure = 1.0;
var __xtSwTone = true;
var __xtSwOutCache = [];
/* Camera view rotation for this frame (set by render). */
var __xtSwView = null;

function __xtSwClamp01(v) {
  return v < 0 ? 0 : v > 1 ? 1 : v;
}

function __xtSwLinToSrgb(v) {
  v = __xtSwClamp01(v);
  return v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055;
}

/* ACES filmic, matching three's ACESFilmicToneMapping closely enough. */
function __xtSwAces(v) {
  if (!(v > 0)) return 0;
  var x = v * __xtSwExposure;
  return (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
}

function __xtSwByte(v) {
  var mapped = __xtSwTone ? __xtSwAces(v) : v * __xtSwExposure;
  return Math.round(__xtSwLinToSrgb(mapped) * 255);
}

function __xtSwCss(r, g, b, a) {
  var R = __xtSwByte(r);
  var G = __xtSwByte(g);
  var B = __xtSwByte(b);
  if (a === undefined || a >= 1) return "rgb(" + R + "," + G + "," + B + ")";
  return "rgba(" + R + "," + G + "," + B + "," + (a < 0 ? 0 : a) + ")";
}

/* Quantised cache for the hundreds of crowd silhouettes. */
function __xtSwCssQuant(r, g, b) {
  var R = __xtSwByte(r);
  var G = __xtSwByte(g);
  var B = __xtSwByte(b);
  var q = ((R >> 4) << 8) | ((G >> 4) << 4) | (B >> 4);
  var s = __xtSwOutCache[q];
  if (s === undefined) {
    s = "rgb(" + ((R >> 4) * 17) + "," + ((G >> 4) * 17) + "," + ((B >> 4) * 17) + ")";
    __xtSwOutCache[q] = s;
  }
  return s;
}

/* -- transform helpers ---------------------------------------------------- */

function __xtSwXform(m, x, y, z, out, o) {
  var e = m.elements;
  out[o] = e[0] * x + e[4] * y + e[8] * z + e[12];
  out[o + 1] = e[1] * x + e[5] * y + e[9] * z + e[13];
  out[o + 2] = e[2] * x + e[6] * y + e[10] * z + e[14];
}

function __xtSwMaxScale(m) {
  var e = m.elements;
  var sx = Math.sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
  var sy = Math.sqrt(e[4] * e[4] + e[5] * e[5] + e[6] * e[6]);
  var sz = Math.sqrt(e[8] * e[8] + e[9] * e[9] + e[10] * e[10]);
  return Math.max(sx, Math.max(sy, sz));
}

/* The scale of the local X/Z plane. A cylinder/capsule is scaled along Y to
 * its length and along X/Z to its radius, so `maxScale` (the length!) would
 * make a rope as thick as it is long. */
function __xtSwRadialScale(m) {
  var e = m.elements;
  var sx = Math.sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
  var sz = Math.sqrt(e[8] * e[8] + e[9] * e[9] + e[10] * e[10]);
  return Math.max(sx, sz);
}

function __xtSwNormal(m, x, y, z, out) {
  var e = m.elements;
  var nx = e[0] * x + e[4] * y + e[8] * z;
  var ny = e[1] * x + e[5] * y + e[9] * z;
  var nz = e[2] * x + e[6] * y + e[10] * z;
  var l = Math.sqrt(nx * nx + ny * ny + nz * nz);
  if (l < 0.000001) {
    out[0] = 0;
    out[1] = 0;
    out[2] = 1;
    return out;
  }
  out[0] = nx / l;
  out[1] = ny / l;
  out[2] = nz / l;
  return out;
}

/* Project a world point through viewProjection; out = [sx, sy, w]. */
function __xtSwProjectTo(vp, x, y, z, W, H, out, o) {
  var e = vp.elements;
  var cx = e[0] * x + e[4] * y + e[8] * z + e[12];
  var cy = e[1] * x + e[5] * y + e[9] * z + e[13];
  var cz = e[2] * x + e[6] * y + e[10] * z + e[14];
  var cw = e[3] * x + e[7] * y + e[11] * z + e[15];
  if (!(cw > 0.00001)) {
    out[o] = 0;
    out[o + 1] = 0;
    out[o + 2] = cw;
    return false;
  }
  var inv = 1 / cw;
  out[o] = (cx * inv * 0.5 + 0.5) * W;
  out[o + 1] = (0.5 - cy * inv * 0.5) * H;
  out[o + 2] = cw;
  return true;
}

/* -- fog ---------------------------------------------------------------- */

var __xtSwFogColor = [0, 0, 0];
var __xtSwFogDensity = 0;
var __xtSwFogNear = 0;
var __xtSwFogFar = 1;
var __xtSwFogExp = false;

function __xtSwFogFactor(dist) {
  if (__xtSwFogDensity > 0) {
    var t = __xtSwFogDensity * dist;
    return 1 - Math.exp(-t * t);
  }
  if (__xtSwFogFar > __xtSwFogNear && __xtSwFogFar > 0) {
    var u = (dist - __xtSwFogNear) / (__xtSwFogFar - __xtSwFogNear);
    return __xtSwClamp01(u);
  }
  return 0;
}

function __xtSwFogMix(c, dist) {
  var f = __xtSwFogFactor(dist);
  if (f <= 0) return c;
  c[0] += (__xtSwFogColor[0] - c[0]) * f;
  c[1] += (__xtSwFogColor[1] - c[1]) * f;
  c[2] += (__xtSwFogColor[2] - c[2]) * f;
  return c;
}

/* -- lighting ----------------------------------------------------------- */

function __xtSwCollectLights(scene) {
  var L = { ambient: [0, 0, 0], hemi: null, dirs: [], points: [], spots: [] };
  scene.traverse(function (o) {
    if (o.isAmbientLight) {
      L.ambient[0] += o.color.r * o.intensity;
      L.ambient[1] += o.color.g * o.intensity;
      L.ambient[2] += o.color.b * o.intensity;
    } else if (o.isHemisphereLight) {
      var h = L.hemi || (L.hemi = { sky: [0, 0, 0], ground: [0, 0, 0], intensity: 0 });
      h.sky[0] += o.color.r * o.intensity;
      h.sky[1] += o.color.g * o.intensity;
      h.sky[2] += o.color.b * o.intensity;
      h.ground[0] += o.groundColor.r * o.intensity;
      h.ground[1] += o.groundColor.g * o.intensity;
      h.ground[2] += o.groundColor.b * o.intensity;
      h.intensity = 1;
    } else if (o.isDirectionalLight) {
      var te = o.target.matrixWorld.elements;
      var pe = o.matrixWorld.elements;
      var dx = pe[12] - te[12];
      var dy = pe[13] - te[13];
      var dz = pe[14] - te[14];
      var len = Math.sqrt(dx * dx + dy * dy + dz * dz);
      if (len > 0.00001) {
        L.dirs.push({
          x: dx / len, y: dy / len, z: dz / len,
          color: [o.color.r, o.color.g, o.color.b],
          intensity: o.intensity
        });
      }
    } else if (o.isSpotLight) {
      var se = o.matrixWorld.elements;
      var ste = o.target.matrixWorld.elements;
      var sx = ste[12] - se[12];
      var sy = ste[13] - se[13];
      var sz = ste[14] - se[14];
      var sl = Math.sqrt(sx * sx + sy * sy + sz * sz);
      if (sl > 0.00001) {
        L.spots.push({
          x: se[12], y: se[13], z: se[14],
          dx: sx / sl, dy: sy / sl, dz: sz / sl,
          color: [o.color.r, o.color.g, o.color.b],
          intensity: o.intensity,
          distance: o.distance || 0,
          decay: o.decay === undefined ? 2 : o.decay,
          cosAngle: Math.cos(o.angle || 0),
          cosPenumbra: Math.cos((o.angle || 0) * (1 - (o.penumbra || 0)))
        });
      }
    } else if (o.isPointLight) {
      var qe = o.matrixWorld.elements;
      L.points.push({
        x: qe[12], y: qe[13], z: qe[14],
        color: [o.color.r, o.color.g, o.color.b],
        intensity: o.intensity,
        distance: o.distance || 0,
        decay: o.decay === undefined ? 2 : o.decay
      });
    }
  });
  return L;
}

function __xtSwDistanceAttenuation(dist, cutoff, decay) {
  var att = 1 / Math.max(Math.pow(dist, decay), 0.01);
  if (cutoff > 0) {
    var k = 1 - Math.pow(dist / cutoff, 4);
    if (k <= 0) return 0;
    att *= k * k;
  }
  return att;
}

/* Irradiance at a point for a surface normal. Returns [r, g, b] (linear). */
function __xtSwLightAt(L, px, py, pz, nx, ny, nz) {
  var r = L.ambient[0];
  var g = L.ambient[1];
  var b = L.ambient[2];
  if (L.hemi) {
    var t = 0.5 + 0.5 * ny;
    var u = 0.5 - 0.5 * ny;
    r += (L.hemi.sky[0] * t + L.hemi.ground[0] * u) * L.hemi.intensity;
    g += (L.hemi.sky[1] * t + L.hemi.ground[1] * u) * L.hemi.intensity;
    b += (L.hemi.sky[2] * t + L.hemi.ground[2] * u) * L.hemi.intensity;
  }
  var i, l, ndl;
  for (i = 0; i < L.dirs.length; i += 1) {
    l = L.dirs[i];
    ndl = nx * l.x + ny * l.y + nz * l.z;
    if (ndl > 0) {
      r += l.color[0] * l.intensity * ndl;
      g += l.color[1] * l.intensity * ndl;
      b += l.color[2] * l.intensity * ndl;
    }
  }
  for (i = 0; i < L.points.length; i += 1) {
    l = L.points[i];
    var dx = l.x - px;
    var dy = l.y - py;
    var dz = l.z - pz;
    var dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 0.0001) continue;
    if (l.distance > 0 && dist > l.distance) continue;
    ndl = (dx * nx + dy * ny + dz * nz) / dist;
    if (ndl <= 0) continue;
    var att = __xtSwDistanceAttenuation(dist, l.distance, l.decay) * l.intensity * ndl;
    r += l.color[0] * att;
    g += l.color[1] * att;
    b += l.color[2] * att;
  }
  for (i = 0; i < L.spots.length; i += 1) {
    l = L.spots[i];
    var sx = l.x - px;
    var sy = l.y - py;
    var sz = l.z - pz;
    var sd = Math.sqrt(sx * sx + sy * sy + sz * sz);
    if (sd < 0.0001) continue;
    if (l.distance > 0 && sd > l.distance) continue;
    ndl = (sx * nx + sy * ny + sz * nz) / sd;
    if (ndl <= 0) continue;
    var c = -(sx * l.dx + sy * l.dy + sz * l.dz) / sd;
    if (c < l.cosAngle) continue;
    var spot = __xtSwClamp01((c - l.cosAngle) / Math.max(l.cosPenumbra - l.cosAngle, 0.0001));
    var satt =
      __xtSwDistanceAttenuation(sd, l.distance, l.decay) * l.intensity * ndl * spot;
    r += l.color[0] * satt;
    g += l.color[1] * satt;
    b += l.color[2] * satt;
  }
  return [r, g, b];
}

/* The strongest light direction at a point, used to place a sphere highlight. */
function __xtSwBestLight(L, px, py, pz) {
  var best = 0;
  var bx = 0.3, by = 0.8, bz = 0.5;
  var i, l, s;
  for (i = 0; i < L.dirs.length; i += 1) {
    l = L.dirs[i];
    s = l.intensity;
    if (s > best) { best = s; bx = l.x; by = l.y; bz = l.z; }
  }
  for (i = 0; i < L.points.length; i += 1) {
    l = L.points[i];
    var dx = l.x - px, dy = l.y - py, dz = l.z - pz;
    var d = Math.sqrt(dx * dx + dy * dy + dz * dz);
    if (d < 0.0001) continue;
    if (l.distance > 0 && d > l.distance) continue;
    s = l.intensity * __xtSwDistanceAttenuation(d, l.distance, l.decay);
    if (s > best) { best = s; bx = dx / d; by = dy / d; bz = dz / d; }
  }
  for (i = 0; i < L.spots.length; i += 1) {
    l = L.spots[i];
    var sx = l.x - px, sy = l.y - py, sz = l.z - pz;
    var sd = Math.sqrt(sx * sx + sy * sy + sz * sz);
    if (sd < 0.0001) continue;
    if (l.distance > 0 && sd > l.distance) continue;
    s = l.intensity * __xtSwDistanceAttenuation(sd, l.distance, l.decay);
    if (s > best) { best = s; bx = sx / sd; by = sy / sd; bz = sz / sd; }
  }
  return [bx, by, bz, best];
}

/* -- per-frame renderer -------------------------------------------------- */

class WebGLRenderer {
  constructor(parameters) {
    parameters = parameters || {};
    var canvas = parameters.canvas;
    if (!canvas) canvas = document.createElement("canvas");
    this.domElement = canvas;
    this._canvas = canvas;
    this._ctx = canvas.getContext("2d");
    if (!this._ctx) throw new Error("gui: 2D canvas context unavailable");
    this._pixelRatio = 1;
    this._width = 0;
    this._height = 0;
    this._bufW = 0;
    this._bufH = 0;
    this._vp = new Matrix4();
    this._tmpA = new Vector3();
    this._tmpB = new Vector3();
    this._camPos = [0, 0, 0];
    this._fpx = 600;
    this._lights = null;
    this._items = [];
    this._alpha = 1;
    this.shadowMap = { enabled: false, type: 0, autoUpdate: true, needsUpdate: false };
    this.capabilities = {
      isWebGL2: true,
      maxTextureSize: 4096,
      precision: "highp",
      getMaxAnisotropy: function () { return 1; }
    };
    this.outputColorSpace = SRGBColorSpace;
    this.toneMapping = NoToneMapping;
    this.toneMappingExposure = 1;
    this.info = { render: { calls: 0, triangles: 0, points: 0, lines: 0 }, memory: {} };
    this._initialized = false;
  }

  setPixelRatio(value) {
    this._pixelRatio = value > 0 ? value : 1;
    this._applySize();
  }

  getPixelRatio() {
    return this._pixelRatio;
  }

  setSize(width, height, updateStyle) {
    this._width = width;
    this._height = height;
    this._updateStyle = updateStyle !== false;
    this._applySize();
  }

  getSize(target) {
    if (target) {
      target.x = this._width;
      target.y = this._height;
      return target;
    }
    return { x: this._width, y: this._height };
  }

  getDrawingBufferSize(target) {
    if (target) {
      target.x = this._bufW;
      target.y = this._bufH;
      return target;
    }
    return { x: this._bufW, y: this._bufH };
  }

  setClearColor() {}
  getClearColor(target) { return target; }
  setClearAlpha() {}
  getClearAlpha() { return 1; }
  setViewport() {}
  setScissor() {}
  setScissorTest() {}
  clear() {}
  clearDepth() {}
  clearColor() {}
  clearStencil() {}
  compile() {}
  dispose() {}
  forceContextLoss() {}
  getContext() { return this._ctx; }
  getRenderTarget() { return null; }
  setRenderTarget() {}
  readRenderTargetPixels() {}
  initTexture() {}
  copyFramebufferToTexture() {}
  resetState() {}

  _applySize() {
    var w = this._width;
    var h = this._height;
    if (!(w > 1) || !(h > 1)) return;
    /* Render the 3D layer at a bounded internal resolution: the HTML UI stays
     * crisp and the Canvas2D rasteriser keeps its per-frame cost predictable. */
    var scale = Math.min(1, 1000 / Math.max(w, 1));
    var bw = Math.max(1, Math.round(w * scale));
    var bh = Math.max(1, Math.round(h * scale));
    if (this._canvas.width !== bw || this._canvas.height !== bh) {
      this._canvas.width = bw;
      this._canvas.height = bh;
    }
    if (this._updateStyle !== false) {
      this._canvas.style.width = w + "px";
      this._canvas.style.height = h + "px";
    }
    this._bufW = bw;
    this._bufH = bh;
  }

  render(scene, camera) {
    if (!scene || !camera) return;
    if (!(this._width > 1) || !(this._height > 1)) {
      var iw = typeof window !== "undefined" ? window.innerWidth : 0;
      var ih = typeof window !== "undefined" ? window.innerHeight : 0;
      if (iw > 1 && ih > 1) this.setSize(iw, ih);
    }
    this._applySize();
    var W = this._bufW;
    var H = this._bufH;
    if (!(W > 1) || !(H > 1)) return;
    var ctx = this._ctx;

    __xtSwExposure = this.toneMappingExposure || 1;
    __xtSwTone = this.toneMapping !== NoToneMapping;

    scene.updateMatrixWorld();
    if (camera.parent === null) camera.updateMatrixWorld();
    camera.matrixWorldInverse.copy(camera.matrixWorld).invert();
    this._vp.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
    __xtSwView = camera.matrixWorldInverse;

    var ce = camera.matrixWorld.elements;
    this._camPos[0] = ce[12];
    this._camPos[1] = ce[13];
    this._camPos[2] = ce[14];
    var fov = camera.fov || 53;
    this._fpx = H / (2 * Math.tan((fov * Math.PI) / 360));
    this._lights = __xtSwCollectLights(scene);

    /* Fog. */
    __xtSwFogDensity = 0;
    __xtSwFogNear = 0;
    __xtSwFogFar = 1;
    if (scene.fog) {
      __xtSwFogColor[0] = scene.fog.color.r;
      __xtSwFogColor[1] = scene.fog.color.g;
      __xtSwFogColor[2] = scene.fog.color.b;
      if (scene.fog.isFogExp2) {
        __xtSwFogDensity = scene.fog.density || 0;
      } else {
        __xtSwFogNear = scene.fog.near || 0;
        __xtSwFogFar = scene.fog.far || 1;
      }
    } else {
      __xtSwFogColor[0] = 0;
      __xtSwFogColor[1] = 0;
      __xtSwFogColor[2] = 0;
    }

    /* Background. */
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.globalAlpha = 1;
    if (scene.background && scene.background.isColor) {
      ctx.fillStyle = __xtSwCss(scene.background.r, scene.background.g, scene.background.b);
    } else {
      ctx.fillStyle = "#000";
    }
    ctx.fillRect(0, 0, W, H);

    /* Collect drawables. */
    var items = [];
    __xtSwWalk(scene, this, items, 0, 0, 0);
    items.sort(function (a, b) {
      if (a.layer !== b.layer) return a.layer - b.layer;
      return b.depth - a.depth;
    });

    for (var i = 0; i < items.length; i += 1) {
      var it = items[i];
      try {
        __xtSwDrawItem(ctx, it, this, W, H);
      } catch (error) {
        if (!this._logged) {
          this._logged = true;
          console.error("gui: three.js draw failed", error);
        }
      }
    }
    ctx.globalAlpha = 1;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    this._items = items;
  }
}

/* Walk the scene and record what to draw and in what order. */
function __xtSwWalk(obj, r, items, px, py, pz) {
  if (obj.visible === false) return;
  var e = obj.matrixWorld ? obj.matrixWorld.elements : null;
  if (e) {
    px = e[12];
    py = e[13];
    pz = e[14];
  }
  if (obj.isInstancedMesh) {
    items.push({ kind: 3, obj: obj, layer: 3, depth: __xtSwCamDist(r, px, py, pz), px: px, py: py, pz: pz });
  } else if (obj.isSprite) {
    if (obj.material && obj.material.visible !== false) {
      items.push({ kind: 1, obj: obj, layer: 6, depth: __xtSwCamDist(r, px, py, pz), px: px, py: py, pz: pz });
    }
  } else if (obj.isPoints) {
    if (obj.material && obj.material.visible !== false) {
      items.push({ kind: 2, obj: obj, layer: 6, depth: __xtSwCamDist(r, px, py, pz), px: px, py: py, pz: pz });
    }
  } else if (obj.isMesh) {
    var geom = obj.geometry;
    var mat = obj.material;
    if (geom && mat && mat.visible !== false && !(mat.opacity <= 0 && mat.transparent)) {
      var layer = __xtSwLayerFor(obj, geom, px, py, pz);
      items.push({
        kind: 0, obj: obj, layer: layer,
        depth: __xtSwCamDist(r, px, py, pz),
        px: px, py: py, pz: pz
      });
    }
  }
  var children = obj.children;
  for (var i = 0; i < children.length; i += 1) __xtSwWalk(children[i], r, items, px, py, pz);
}

function __xtSwCamDist(r, x, y, z) {
  var dx = x - r._camPos[0];
  var dy = y - r._camPos[1];
  var dz = z - r._camPos[2];
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

function __xtSwLayerFor(obj, geom, x, y, z) {
  var type = geom.type;
  if (type === "PlaneGeometry") {
    var n = [0, 0, 0];
    __xtSwNormal(obj.matrixWorld, 0, 0, 1, n);
    if (n[1] > 0.5) return 0;
    if (z < -10) return 1;
    return 2;
  }
  if (type === "BoxGeometry" && geom.parameters) {
    var p = geom.parameters;
    if (p.width > 4 && p.depth > 4) return 0;
  }
  if (y > 5) return 2;
  if (obj.isInstancedMesh) return 3;
  if (obj.material && obj.material.transparent) return 6;
  return 4;
}

function __xtSwDrawItem(ctx, it, r, W, H) {
  var obj = it.obj;
  if (it.kind === 0) __xtSwDrawMesh(ctx, obj, r, W, H, it);
  else if (it.kind === 1) __xtSwDrawSprite(ctx, obj, r, W, H);
  else if (it.kind === 2) __xtSwDrawPoints(ctx, obj, r, W, H);
  else if (it.kind === 3) __xtSwDrawInstances(ctx, obj, r, W, H);
}


/* -- meshes -------------------------------------------------------------- */

function __xtSwDrawMesh(ctx, obj, r, W, H, it) {
  var geom = obj.geometry;
  var mat = obj.material;
  var type = geom.type;
  if (type === "SphereGeometry" || type === "IcosahedronGeometry" ||
      type === "OctahedronGeometry" || type === "DodecahedronGeometry" ||
      type === "TetrahedronGeometry") {
    __xtSwDrawSphere(ctx, obj, mat, geom, r, W, H);
  } else if (type === "CapsuleGeometry") {
    __xtSwDrawCapsule(ctx, obj, mat, geom, r, W, H);
  } else if (type === "CylinderGeometry") {
    __xtSwDrawCylinder(ctx, obj, mat, geom, r, W, H);
  } else if (type === "ConeGeometry") {
    __xtSwDrawCone(ctx, obj, mat, geom, r, W, H);
  } else if (type === "BoxGeometry") {
    __xtSwDrawBox(ctx, obj, mat, geom, r, W, H);
  } else if (type === "PlaneGeometry") {
    __xtSwDrawPlane(ctx, obj, mat, geom, r, W, H, it);
  } else if (type === "TorusGeometry") {
    __xtSwDrawTorus(ctx, obj, mat, geom, r, W, H);
  } else {
    __xtSwDrawFallbackMesh(ctx, obj, mat, geom, r, W, H);
  }
}

function __xtSwMaterialColor(mat) {
  if (mat && mat.color) return [mat.color.r, mat.color.g, mat.color.b];
  return [1, 1, 1];
}

function __xtSwIsLit(mat) {
  return !!(mat && (mat.isMeshStandardMaterial || mat.isMeshPhysicalMaterial ||
    mat.isMeshLambertMaterial || mat.isMeshPhongMaterial));
}

function __xtSwBaseColor(mat, L, x, y, z, nx, ny, nz, dist) {
  var c = __xtSwMaterialColor(mat);
  if (!__xtSwIsLit(mat)) {
    var o = __xtSwFogMix([c[0], c[1], c[2]], dist);
    return o;
  }
  var lit = __xtSwLightAt(L, x, y, z, nx, ny, nz);
  if (mat.emissive) {
    var ei = mat.emissiveIntensity === undefined ? 1 : mat.emissiveIntensity;
    lit[0] += mat.emissive.r * ei;
    lit[1] += mat.emissive.g * ei;
    lit[2] += mat.emissive.b * ei;
  }
  var o2 = [c[0] * lit[0], c[1] * lit[1], c[2] * lit[2]];
  return __xtSwFogMix(o2, dist);
}

/* Project a world point; returns [sx, sy, w] or null. */
function __xtSwProj(r, x, y, z) {
  var p = [0, 0, 0];
  if (!__xtSwProjectTo(r._vp, x, y, z, r._bufW, r._bufH, p, 0)) return null;
  return p;
}

function __xtSwHemiNormal(px, py, pz, r) {
  var dx = r._camPos[0] - px;
  var dy = r._camPos[1] - py;
  var dz = r._camPos[2] - pz;
  var l = Math.sqrt(dx * dx + dy * dy + dz * dz);
  if (l < 0.00001) return [0, 0, 1];
  return [dx / l, dy / l, dz / l];
}

function __xtSwSphereColors(mat, L, px, py, pz, dist, r) {
  var c = __xtSwMaterialColor(mat);
  if (!__xtSwIsLit(mat)) {
    var flat = __xtSwFogMix([c[0], c[1], c[2]], dist);
    var oa = mat.transparent ? (mat.opacity === undefined ? 1 : mat.opacity) : 1;
    return { bright: flat, mid: flat, dark: flat, alpha: oa, best: [0, 1, 0, 1] };
  }
  var n = __xtSwHemiNormal(px, py, pz, r);
  var lit = __xtSwLightAt(L, px, py, pz, n[0], n[1], n[2]);
  var best = __xtSwBestLight(L, px, py, pz);
  var bs = Math.min(best[3], 4);
  var spec = 0.04 + (mat.metalness || 0) * 0.45;
  var o = mat.transparent ? (mat.opacity === undefined ? 1 : mat.opacity) : 1;
  var bright = __xtSwFogMix([c[0] * (lit[0] + bs * 0.35) + spec,
    c[1] * (lit[1] + bs * 0.35) + spec,
    c[2] * (lit[2] + bs * 0.35) + spec], dist);
  var mid = __xtSwFogMix([c[0] * lit[0] * 0.82 + spec * 0.25,
    c[1] * lit[1] * 0.82 + spec * 0.25,
    c[2] * lit[2] * 0.82 + spec * 0.25], dist);
  var dark = __xtSwFogMix([c[0] * lit[0] * 0.30,
    c[1] * lit[1] * 0.30,
    c[2] * lit[2] * 0.30], dist);
  return { bright: bright, mid: mid, dark: dark, alpha: o, best: best };
}

function __xtSwDrawSphere(ctx, obj, mat, geom, r, W, H) {
  var p = geom.parameters || {};
  var rad = p.radius !== undefined ? p.radius : 1;
  var s = __xtSwMaxScale(obj.matrixWorld);
  var rw = rad * s;
  var e = obj.matrixWorld.elements;
  var px = e[12], py = e[13], pz = e[14];
  var pr = __xtSwProj(r, px, py, pz);
  if (!pr) return;
  var dist = __xtSwCamDist(r, px, py, pz);
  var rpx = (rw * r._fpx) / Math.max(dist, 0.05);
  if (rpx < 0.35) return;
  if (pr[0] < -rpx * 2 || pr[0] > W + rpx * 2 || pr[1] < -rpx * 2 || pr[1] > H + rpx * 2) return;
  var cols = __xtSwSphereColors(mat, r._lights, px, py, pz, dist, r);
  var off = rpx * 0.42;
  var ve = r._camPos;
  var best = cols.best;
  var bvx = best[0], bvy = best[1], bvz = best[2];
  /* Direction in view space, then screen space, for the highlight offset. */
  var me = __xtSwEyeMatrix(r);
  var vx = me[0] * bvx + me[4] * bvy + me[8] * bvz;
  var vy = me[1] * bvx + me[5] * bvy + me[9] * bvz;
  var vz = me[2] * bvx + me[6] * bvy + me[10] * bvz;
  var vl = Math.sqrt(vx * vx + vy * vy);
  var ox = 0, oy = 0;
  if (vl > 0.0001) { ox = (vx / vl) * off; oy = -(vy / vl) * off; }
  if (vz < 0) { ox *= 0.3; oy *= 0.3; }
  if (rpx > 20000) rpx = 20000;
  ctx.globalAlpha = cols.alpha;
  var g = ctx.createRadialGradient(pr[0] + ox, pr[1] + oy, 0, pr[0], pr[1], rpx * 1.15);
  if (g === null) {
    ctx.fillStyle = __xtSwCss(cols.mid[0], cols.mid[1], cols.mid[2]);
  } else {
    g.addColorStop(0, __xtSwCss(cols.bright[0], cols.bright[1], cols.bright[2]));
    g.addColorStop(0.55, __xtSwCss(cols.mid[0], cols.mid[1], cols.mid[2]));
    g.addColorStop(1, __xtSwCss(cols.dark[0], cols.dark[1], cols.dark[2]));
    ctx.fillStyle = g;
  }
  ctx.beginPath();
  ctx.ellipse(pr[0], pr[1], rpx, rpx, 0, 0, __xtSwTAU);
  ctx.fill();
}

function __xtSwEyeMatrix(r) {
  /* View-space rotation of the current camera. */
  return __xtSwView ? __xtSwView.elements : new Matrix4().elements;
}

function __xtSwDrawCapsule(ctx, obj, mat, geom, r, W, H) {
  var p = geom.parameters || {};
  var rad = p.radius !== undefined ? p.radius : 0.1;
  var half = (p.length !== undefined ? p.length : 1) * 0.5;
  var m = obj.matrixWorld;
  var s = __xtSwRadialScale(m);
  var a = [0, 0, 0], b = [0, 0, 0];
  __xtSwXform(m, 0, half, 0, a, 0);
  __xtSwXform(m, 0, -half, 0, b, 0);
  var pa = __xtSwProj(r, a[0], a[1], a[2]);
  var pb = __xtSwProj(r, b[0], b[1], b[2]);
  if (!pa || !pb) return;
  var mx = (a[0] + b[0]) * 0.5, my = (a[1] + b[1]) * 0.5, mz = (a[2] + b[2]) * 0.5;
  var dist = __xtSwCamDist(r, mx, my, mz);
  var rpx = (rad * s * r._fpx) / Math.max(dist, 0.05);
  var ax = pb[0] - pa[0], ay = pb[1] - pa[1];
  var al = Math.sqrt(ax * ax + ay * ay);
  var nx = al > 0.001 ? -ay / al : 0;
  var ny = al > 0.001 ? ax / al : 1;
  var cols = __xtSwSphereColors(mat, r._lights, mx, my, mz, dist, r);
  var best = cols.best;
  var me = __xtSwEyeMatrix(r);
  var vx = me[0] * best[0] + me[4] * best[1] + me[8] * best[2];
  var vy = me[1] * best[0] + me[5] * best[1] + me[9] * best[2];
  var sign = vx * nx + vy * ny >= 0 ? 1 : -1;
  if (rpx < 0.6) {
    ctx.globalAlpha = cols.alpha;
    ctx.strokeStyle = __xtSwCss(cols.mid[0], cols.mid[1], cols.mid[2]);
    ctx.lineWidth = Math.max(1, rpx * 2);
    ctx.beginPath();
    ctx.moveTo(pa[0], pa[1]);
    ctx.lineTo(pb[0], pb[1]);
    ctx.stroke();
    return;
  }
  ctx.globalAlpha = cols.alpha;
  var mpx = (pa[0] + pb[0]) * 0.5;
  var mpy = (pa[1] + pb[1]) * 0.5;
  var g = ctx.createLinearGradient(
    mpx + nx * rpx * sign, mpy + ny * rpx * sign,
    mpx - nx * rpx * sign, mpy - ny * rpx * sign
  );
  if (g === null) {
    ctx.strokeStyle = __xtSwCss(cols.mid[0], cols.mid[1], cols.mid[2]);
  } else {
    g.addColorStop(0, __xtSwCss(cols.bright[0], cols.bright[1], cols.bright[2]));
    g.addColorStop(0.6, __xtSwCss(cols.mid[0], cols.mid[1], cols.mid[2]));
    g.addColorStop(1, __xtSwCss(cols.dark[0], cols.dark[1], cols.dark[2]));
    ctx.strokeStyle = g;
  }
  ctx.lineWidth = Math.max(1, rpx * 2);
  ctx.lineCap = "round";
  ctx.beginPath();
  ctx.moveTo(pa[0], pa[1]);
  ctx.lineTo(pb[0], pb[1]);
  ctx.stroke();
}

function __xtSwDrawCylinder(ctx, obj, mat, geom, r, W, H) {
  var p = geom.parameters || {};
  var h = p.height !== undefined ? p.height : 1;
  var rt = p.radiusTop !== undefined ? p.radiusTop : 0.1;
  var rb = p.radiusBottom !== undefined ? p.radiusBottom : rt;
  var m = obj.matrixWorld;
  var s = __xtSwRadialScale(m);
  var a = [0, 0, 0], b = [0, 0, 0];
  __xtSwXform(m, 0, h * 0.5, 0, a, 0);
  __xtSwXform(m, 0, -h * 0.5, 0, b, 0);
  var pa = __xtSwProj(r, a[0], a[1], a[2]);
  var pb = __xtSwProj(r, b[0], b[1], b[2]);
  if (!pa || !pb) return;
  var mx = (a[0] + b[0]) * 0.5, my = (a[1] + b[1]) * 0.5, mz = (a[2] + b[2]) * 0.5;
  var dist = __xtSwCamDist(r, mx, my, mz);
  var pxPer = r._fpx / Math.max(dist, 0.05);
  var ra = rt * s * pxPer;
  var rbx = rb * s * pxPer;
  if (ra < 0.35 && rbx < 0.35) return;
  var ax = pb[0] - pa[0], ay = pb[1] - pa[1];
  var al = Math.sqrt(ax * ax + ay * ay);
  var nx = al > 0.001 ? -ay / al : 0;
  var ny = al > 0.001 ? ax / al : 1;
  var cols = __xtSwSphereColors(mat, r._lights, mx, my, mz, dist, r);
  ctx.globalAlpha = cols.alpha;
  if (ra < 0.5 || rbx < 0.5) {
    ctx.strokeStyle = __xtSwCss(cols.mid[0], cols.mid[1], cols.mid[2]);
    ctx.lineWidth = Math.max(1, (ra + rbx));
    ctx.beginPath();
    ctx.moveTo(pa[0], pa[1]);
    ctx.lineTo(pb[0], pb[1]);
    ctx.stroke();
    return;
  }
  var g = ctx.createLinearGradient(
    mx + nx * Math.max(ra, rbx), my + ny * Math.max(ra, rbx),
    mx - nx * Math.max(ra, rbx), my - ny * Math.max(ra, rbx)
  );
  if (g === null) {
    ctx.fillStyle = __xtSwCss(cols.mid[0], cols.mid[1], cols.mid[2]);
  } else {
    g.addColorStop(0, __xtSwCss(cols.bright[0], cols.bright[1], cols.bright[2]));
    g.addColorStop(0.6, __xtSwCss(cols.mid[0], cols.mid[1], cols.mid[2]));
    g.addColorStop(1, __xtSwCss(cols.dark[0], cols.dark[1], cols.dark[2]));
    ctx.fillStyle = g;
  }
  ctx.beginPath();
  ctx.moveTo(pa[0] + nx * ra, pa[1] + ny * ra);
  ctx.lineTo(pa[0] - nx * ra, pa[1] - ny * ra);
  ctx.lineTo(pb[0] - nx * rbx, pb[1] - ny * rbx);
  ctx.lineTo(pb[0] + nx * rbx, pb[1] + ny * rbx);
  ctx.closePath();
  ctx.fill();
}

function __xtSwDrawCone(ctx, obj, mat, geom, r, W, H) {
  var p = geom.parameters || {};
  var h = p.height !== undefined ? p.height : 1;
  var rad = p.radius !== undefined ? p.radius : 0.5;
  var m = obj.matrixWorld;
  var s = __xtSwRadialScale(m);
  var apex = [0, 0, 0], base = [0, 0, 0];
  __xtSwXform(m, 0, h * 0.5, 0, apex, 0);
  __xtSwXform(m, 0, -h * 0.5, 0, base, 0);
  var pa = __xtSwProj(r, apex[0], apex[1], apex[2]);
  var pb = __xtSwProj(r, base[0], base[1], base[2]);
  if (!pa || !pb) return;
  var dist = __xtSwCamDist(r, base[0], base[1], base[2]);
  var rpx = (rad * s * r._fpx) / Math.max(dist, 0.05);
  if (rpx < 0.5) return;
  var ax = pb[0] - pa[0], ay = pb[1] - pa[1];
  var al = Math.sqrt(ax * ax + ay * ay);
  var nx = al > 0.001 ? -ay / al : 0;
  var ny = al > 0.001 ? ax / al : 1;
  var c = __xtSwMaterialColor(mat);
  var alpha = (mat.opacity === undefined ? 1 : mat.opacity);
  ctx.globalAlpha = alpha;
  ctx.fillStyle = __xtSwCss(c[0], c[1], c[2], 1);
  ctx.beginPath();
  ctx.moveTo(pa[0], pa[1]);
  ctx.lineTo(pb[0] + nx * rpx, pb[1] + ny * rpx);
  ctx.lineTo(pb[0] - nx * rpx, pb[1] - ny * rpx);
  ctx.closePath();
  ctx.fill();
}

function __xtSwDrawBox(ctx, obj, mat, geom, r, W, H) {
  var p = geom.parameters || {};
  var hw = (p.width !== undefined ? p.width : 1) * 0.5;
  var hh = (p.height !== undefined ? p.height : 1) * 0.5;
  var hd = (p.depth !== undefined ? p.depth : 1) * 0.5;
  var m = obj.matrixWorld;
  var e = m.elements;
  var pts = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  var corners = [
    [-hw, -hh, -hd], [hw, -hh, -hd], [hw, hh, -hd], [-hw, hh, -hd],
    [-hw, -hh, hd], [hw, -hh, hd], [hw, hh, hd], [-hw, hh, hd]
  ];
  var faces = [
    [4, 5, 6, 7], [1, 0, 3, 2], [0, 4, 7, 3],
    [5, 1, 2, 6], [3, 7, 6, 2], [0, 1, 5, 4]
  ];
  var i, j;
  for (i = 0; i < 8; i += 1) {
    var c = corners[i];
    __xtSwProjectTo(r._vp, e[0] * c[0] + e[4] * c[1] + e[8] * c[2] + e[12],
      e[1] * c[0] + e[5] * c[1] + e[9] * c[2] + e[13],
      e[2] * c[0] + e[6] * c[1] + e[10] * c[2] + e[14], W, H, pts, i * 3);
  }
  var side = mat.side === undefined ? 0 : mat.side;
  var alpha = mat.transparent ? (mat.opacity === undefined ? 1 : mat.opacity) : 1;
  for (i = 0; i < 6; i += 1) {
    var f = faces[i];
    var w0 = pts[f[0] * 3 + 2];
    var w1 = pts[f[1] * 3 + 2];
    var w2 = pts[f[2] * 3 + 2];
    var w3 = pts[f[3] * 3 + 2];
    if (w0 <= 0 || w1 <= 0 || w2 <= 0 || w3 <= 0) continue;
    var af = corners[f[0]];
    var bf = corners[f[1]];
    var cf = corners[f[2]];
    var wx = (af[0] + cf[0]) * 0.5;
    var wy = (af[1] + cf[1]) * 0.5;
    var wz = (af[2] + cf[2]) * 0.5;
    var ux = bf[0] - af[0], uy = bf[1] - af[1], uz = bf[2] - af[2];
    var vx = cf[0] - af[0], vy = cf[1] - af[1], vz = cf[2] - af[2];
    var lnx = uy * vz - uz * vy;
    var lny = uz * vx - ux * vz;
    var lnz = ux * vy - uy * vx;
    var nl = Math.sqrt(lnx * lnx + lny * lny + lnz * lnz);
    if (nl < 0.000001) continue;
    var nrm = [0, 0, 0];
    __xtSwNormal(m, lnx / nl, lny / nl, lnz / nl, nrm);
    var nx = nrm[0], ny = nrm[1], nz = nrm[2];
    var wc = [0, 0, 0];
    __xtSwXform(m, wx, wy, wz, wc, 0);
    var vdx = r._camPos[0] - wc[0], vdy = r._camPos[1] - wc[1], vdz = r._camPos[2] - wc[2];
    var facing = nx * vdx + ny * vdy + nz * vdz;
    if (side === 0 && facing <= 0) continue;
    if (side === 1 && facing >= 0) continue;
    var col = __xtSwBaseColor(mat, r._lights, wc[0], wc[1], wc[2], nx, ny, nz,
      __xtSwCamDist(r, wc[0], wc[1], wc[2]));
    ctx.globalAlpha = alpha;
    ctx.fillStyle = __xtSwCss(col[0], col[1], col[2]);
    ctx.beginPath();
    ctx.moveTo(pts[f[0] * 3], pts[f[0] * 3 + 1]);
    ctx.lineTo(pts[f[1] * 3], pts[f[1] * 3 + 1]);
    ctx.lineTo(pts[f[2] * 3], pts[f[2] * 3 + 1]);
    ctx.lineTo(pts[f[3] * 3], pts[f[3] * 3 + 1]);
    ctx.closePath();
    ctx.fill();
  }
}

function __xtSwDrawTorus(ctx, obj, mat, geom, r, W, H) {
  var p = geom.parameters || {};
  var rad = (p.radius !== undefined ? p.radius : 0.1) + (p.tube !== undefined ? p.tube : 0.01);
  var tube = p.tube !== undefined ? p.tube : 0.01;
  var s = __xtSwMaxScale(obj.matrixWorld);
  var e = obj.matrixWorld.elements;
  var pr = __xtSwProj(r, e[12], e[13], e[14]);
  if (!pr) return;
  var dist = __xtSwCamDist(r, e[12], e[13], e[14]);
  var rpx = (rad * s * r._fpx) / Math.max(dist, 0.05);
  var tpx = ((tube * s * r._fpx) / Math.max(dist, 0.05)) * 2;
  if (rpx < 0.5) return;
  var col = __xtSwBaseColor(mat, r._lights, e[12], e[13], e[14], 0, 1, 0, dist);
  ctx.globalAlpha = mat.transparent ? (mat.opacity === undefined ? 1 : mat.opacity) : 1;
  ctx.strokeStyle = __xtSwCss(col[0], col[1], col[2]);
  ctx.lineWidth = Math.max(1, tpx);
  ctx.beginPath();
  ctx.ellipse(pr[0], pr[1], rpx, rpx, 0, 0, __xtSwTAU);
  ctx.stroke();
}

/* -- planes and textures ------------------------------------------------- */

function __xtSwPlaneCorners(obj, geom, out) {
  var p = geom.parameters || {};
  var hw = (p.width !== undefined ? p.width : 1) * 0.5;
  var hh = (p.height !== undefined ? p.height : 1) * 0.5;
  __xtSwXform(obj.matrixWorld, -hw, hh, 0, out, 0);
  __xtSwXform(obj.matrixWorld, hw, hh, 0, out, 3);
  __xtSwXform(obj.matrixWorld, -hw, -hh, 0, out, 6);
  __xtSwXform(obj.matrixWorld, hw, -hh, 0, out, 9);
}

function __xtSwDrawPlane(ctx, obj, mat, geom, r, W, H, it) {
  var corners = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  __xtSwPlaneCorners(obj, geom, corners);
  var n = [0, 0, 0];
  __xtSwNormal(obj.matrixWorld, 0, 0, 1, n);
  var cx = (corners[0] + corners[9]) * 0.5;
  var cy = (corners[1] + corners[10]) * 0.5;
  var cz = (corners[2] + corners[11]) * 0.5;
  var vdx = r._camPos[0] - cx, vdy = r._camPos[1] - cy, vdz = r._camPos[2] - cz;
  var facing = n[0] * vdx + n[1] * vdy + n[2] * vdz;
  var side = mat.side === undefined ? 0 : mat.side;
  if (side === 0 && facing <= 0) return;
  if (side === 1 && facing >= 0) return;

  var tex = mat.map;
  var img = tex && tex.image ? tex.image : null;
  var hasTex = !!(img && img.width > 0 && img.height > 0);
  var dist = __xtSwCamDist(r, cx, cy, cz);
  if (hasTex) {
    __xtSwTexturedPlane(ctx, obj, mat, geom, corners, img, tex, r, W, H);
    return;
  }
  var col = __xtSwBaseColor(mat, r._lights, cx, cy, cz, n[0], n[1], n[2], dist);
  var alpha = mat.transparent ? (mat.opacity === undefined ? 1 : mat.opacity) : 1;
  var p0 = __xtSwProj(r, corners[0], corners[1], corners[2]);
  var p1 = __xtSwProj(r, corners[3], corners[4], corners[5]);
  var p2 = __xtSwProj(r, corners[6], corners[7], corners[8]);
  var p3 = __xtSwProj(r, corners[9], corners[10], corners[11]);
  if (!p0 || !p1 || !p2 || !p3) return;
  ctx.globalAlpha = alpha;
  ctx.fillStyle = __xtSwCss(col[0], col[1], col[2]);
  ctx.beginPath();
  ctx.moveTo(p0[0], p0[1]);
  ctx.lineTo(p1[0], p1[1]);
  ctx.lineTo(p3[0], p3[1]);
  ctx.lineTo(p2[0], p2[1]);
  ctx.closePath();
  ctx.fill();
}

function __xtSwTexturedPlane(ctx, obj, mat, geom, corners, img, tex, r, W, H) {
  var p = geom.parameters || {};
  var w = p.width !== undefined ? p.width : 1;
  var h = p.height !== undefined ? p.height : 1;
  var normal = [0, 0, 0];
  __xtSwNormal(obj.matrixWorld, 0, 0, 1, normal);
  var perspective = normal[1] > 0.5;
  var cols = perspective ? 28 : 10;
  var rows = perspective ? 28 : 6;
  if (w > 20) cols = 12;
  if (h > 10) rows = 8;
  var cx = (corners[0] + corners[9]) * 0.5;
  var cy = (corners[1] + corners[10]) * 0.5;
  var cz = (corners[2] + corners[11]) * 0.5;
  var dist = __xtSwCamDist(r, cx, cy, cz);
  var texW = img.width;
  var texH = img.height;
  var flip = tex.flipY !== false;
  var scratch = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  var u, v, i, j;
  ctx.globalAlpha = mat.transparent ? (mat.opacity === undefined ? 1 : mat.opacity) : 1;
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  for (j = 0; j < rows; j += 1) {
    var v0 = j / rows;
    var v1 = (j + 1) / rows;
    for (i = 0; i < cols; i += 1) {
      var u0 = i / cols;
      var u1 = (i + 1) / cols;
      /* Corners: top edge (v=1) TL/TR, bottom edge (v=0) BL/BR. */
      __xtSwBilerp(corners, u0, v1, scratch, 0);
      __xtSwBilerp(corners, u1, v1, scratch, 3);
      __xtSwBilerp(corners, u0, v0, scratch, 6);
      __xtSwBilerp(corners, u1, v0, scratch, 9);
      var p00 = [0, 0, 0], p10 = [0, 0, 0], p01 = [0, 0, 0];
      if (!__xtSwProjectTo(r._vp, scratch[0], scratch[1], scratch[2], W, H, p00, 0)) continue;
      if (!__xtSwProjectTo(r._vp, scratch[3], scratch[4], scratch[5], W, H, p10, 0)) continue;
      if (!__xtSwProjectTo(r._vp, scratch[6], scratch[7], scratch[8], W, H, p01, 0)) continue;
      if (p10[0] - p00[0] === 0 && p10[1] - p00[1] === 0) continue;
      var minW = p00[2];
      if (p10[2] < minW) minW = p10[2];
      if (p01[2] < minW) minW = p01[2];
      var bx0 = Math.min(p00[0], Math.min(p10[0], p01[0]));
      var bx1 = Math.max(p00[0], Math.max(p10[0], p01[0]));
      var by0 = Math.min(p00[1], Math.min(p10[1], p01[1]));
      var by1 = Math.max(p00[1], Math.max(p10[1], p01[1]));
      if (minW < 0.15) continue;
      if (bx1 - bx0 > W * 2.5 || by1 - by0 > H * 2.5) continue;
      var ux = (p10[0] - p00[0]) * 1.015;
      var uy = (p10[1] - p00[1]) * 1.015;
      var vx = (p01[0] - p00[0]) * 1.015;
      var vy = (p01[1] - p00[1]) * 1.015;
      var ox = p00[0] - ux * 0.0075;
      var oy = p00[1] - uy * 0.0075;
      var sx = u0 * texW;
      var sw = (u1 - u0) * texW;
      var sy = flip ? (1 - v1) * texH : v0 * texH;
      var sh = (v1 - v0) * texH;
      if (sw < 0.5 || sh < 0.5) continue;
      ctx.setTransform(ux, uy, vx, vy, ox, oy);
      ctx.drawImage(img, 0, 0, 1, 1, sx, sy, sw, sh);
    }
  }
  ctx.setTransform(1, 0, 0, 1, 0, 0);

  /* Material tint and fog over the whole plane. */
  var p0 = __xtSwProj(r, corners[0], corners[1], corners[2]);
  var p1 = __xtSwProj(r, corners[3], corners[4], corners[5]);
  var p2 = __xtSwProj(r, corners[9], corners[10], corners[11]);
  var p3 = __xtSwProj(r, corners[6], corners[7], corners[8]);
  if (!p0 || !p1 || !p2 || !p3) return;
  var c = __xtSwMaterialColor(mat);
  var tint = Math.max(c[0], Math.max(c[1], c[2]));
  if (tint < 0.999) {
    var srgb = __xtSwLinToSrgb(tint);
    ctx.globalAlpha = __xtSwClamp01(1 - (0.35 + 0.65 * srgb));
    ctx.fillStyle = "#000";
    ctx.beginPath();
    ctx.moveTo(p0[0], p0[1]);
    ctx.lineTo(p1[0], p1[1]);
    ctx.lineTo(p2[0], p2[1]);
    ctx.lineTo(p3[0], p3[1]);
    ctx.closePath();
    ctx.fill();
  }
  var fog = __xtSwFogFactor(dist);
  if (fog > 0.002) {
    ctx.globalAlpha = fog * 0.75;
    ctx.fillStyle = __xtSwCss(__xtSwFogColor[0], __xtSwFogColor[1], __xtSwFogColor[2], 1);
    ctx.beginPath();
    ctx.moveTo(p0[0], p0[1]);
    ctx.lineTo(p1[0], p1[1]);
    ctx.lineTo(p2[0], p2[1]);
    ctx.lineTo(p3[0], p3[1]);
    ctx.closePath();
    ctx.fill();
  }
  if (mat.transparent) ctx.globalAlpha = mat.opacity === undefined ? 1 : mat.opacity;
  else ctx.globalAlpha = 1;
}

/* Bilinear interpolation of a plane's four world corners.
 * `corners` is TL(0..2), TR(3..5), BL(6..8), BR(9..11); v=1 is the top edge. */
function __xtSwBilerp(corners, u, v, out, o) {
  var x0 = corners[0] + (corners[3] - corners[0]) * u;
  var y0 = corners[1] + (corners[4] - corners[1]) * u;
  var z0 = corners[2] + (corners[5] - corners[2]) * u;
  var x1 = corners[6] + (corners[9] - corners[6]) * u;
  var y1 = corners[7] + (corners[10] - corners[7]) * u;
  var z1 = corners[8] + (corners[11] - corners[8]) * u;
  out[o] = x0 + (x1 - x0) * (1 - v);
  out[o + 1] = y0 + (y1 - y0) * (1 - v);
  out[o + 2] = z0 + (z1 - z0) * (1 - v);
}

/* -- sprites / points / instances ---------------------------------------- */

function __xtSwDrawSprite(ctx, obj, r, W, H) {
  var mat = obj.material;
  var e = obj.matrixWorld.elements;
  var pr = __xtSwProj(r, e[12], e[13], e[14]);
  if (!pr) return;
  var dist = __xtSwCamDist(r, e[12], e[13], e[14]);
  var sx = obj.scale ? Math.abs(obj.scale.x) : 1;
  var sy = obj.scale ? Math.abs(obj.scale.y) : 1;
  var px = (sx * r._fpx) / Math.max(dist, 0.05);
  var py = (sy * r._fpx) / Math.max(dist, 0.05);
  if (px < 0.5 || py < 0.5) return;
  var alpha = mat.opacity === undefined ? 1 : mat.opacity;
  if (pr[0] < -px || pr[0] > W + px || pr[1] < -py || pr[1] > H + py) return;
  var c = __xtSwMaterialColor(mat);
  ctx.globalAlpha = alpha;
  if (mat.map && mat.map.image && mat.map.image.width > 0) {
    ctx.drawImage(mat.map.image, pr[0] - px * 0.5, pr[1] - py * 0.5, px, py);
  } else {
    ctx.fillStyle = __xtSwCss(c[0], c[1], c[2], 1);
    ctx.beginPath();
    ctx.ellipse(pr[0], pr[1], px * 0.5, py * 0.5, 0, 0, __xtSwTAU);
    ctx.fill();
  }
}

function __xtSwDrawPoints(ctx, obj, r, W, H) {
  var geo = obj.geometry;
  var mat = obj.material;
  if (!geo || !geo.attributes || !geo.attributes.position) return;
  var attr = geo.attributes.position;
  var arr = attr.array;
  var count = attr.count;
  var size = mat.size === undefined ? 1 : mat.size;
  var att = mat.sizeAttenuation !== false;
  var alpha = mat.opacity === undefined ? 1 : mat.opacity;
  var c = __xtSwMaterialColor(mat);
  var col = __xtSwCss(c[0], c[1], c[2], 1);
  var img = mat.map && mat.map.image && mat.map.image.width > 0 ? mat.map.image : null;
  var p = [0, 0, 0];
  ctx.globalAlpha = alpha;
  ctx.fillStyle = col;
  var big = 0;
  for (var i = 0; i < count; i += 1) {
    var x = arr[i * 3], y = arr[i * 3 + 1], z = arr[i * 3 + 2];
    if (!__xtSwProjectTo(r._vp, x, y, z, W, H, p, 0)) continue;
    var dist = __xtSwCamDist(r, x, y, z);
    var px = att ? (size * r._fpx) / Math.max(dist, 0.05) : size;
    if (px < 0.75) px = 0.75;
    if (p[0] < -px || p[0] > W + px || p[1] < -px || p[1] > H + px) continue;
    if (img !== null && px > 2.5) {
      ctx.globalAlpha = alpha;
      ctx.drawImage(img, p[0] - px * 0.5, p[1] - px * 0.5, px, px);
      big += 1;
    } else {
      var rp = px * 0.5;
      if (rp < 0.75) rp = 0.75;
      ctx.fillRect(p[0] - rp, p[1] - rp, rp * 2, rp * 2);
    }
  }
  ctx.globalAlpha = 1;
}

function __xtSwDrawInstances(ctx, obj, r, W, H) {
  var geo = obj.geometry;
  var mat = obj.material;
  if (!geo || !obj.instanceMatrix) return;
  if (!geo.boundingBox) geo.computeBoundingBox();
  var bb = geo.boundingBox;
  var hx = 0.15, hy = 0.15;
  if (bb) {
    hx = Math.max((bb.max.x - bb.min.x) * 0.5, (bb.max.z - bb.min.z) * 0.5);
    hy = (bb.max.y - bb.min.y) * 0.5;
  }
  var mArr = obj.instanceMatrix.array;
  var count = obj.count;
  var cArr = obj.instanceColor ? obj.instanceColor.array : null;
  var base = __xtSwMaterialColor(mat);
  var om = obj.matrixWorld;
  var oe = om.elements;
  var p = [0, 0, 0];
  var cls = obj.instanceColor ? null : __xtSwCss(base[0], base[1], base[2], 1);
  var alpha = mat.transparent ? (mat.opacity === undefined ? 1 : mat.opacity) : 1;
  ctx.globalAlpha = alpha;
  for (var i = 0; i < count; i += 1) {
    var o = i * 16;
    var lx = mArr[o + 12], ly = mArr[o + 13], lz = mArr[o + 14];
    var wx = oe[0] * lx + oe[4] * ly + oe[8] * lz + oe[12];
    var wy = oe[1] * lx + oe[5] * ly + oe[9] * lz + oe[13];
    var wz = oe[2] * lx + oe[6] * ly + oe[10] * lz + oe[14];
    if (!__xtSwProjectTo(r._vp, wx, wy, wz, W, H, p, 0)) continue;
    var dist = __xtSwCamDist(r, wx, wy, wz);
    var instScale = __xtSwMaxScale(obj.matrixWorld) * __xtSwMaxScaleFromArray(mArr, o);
    var sx = (hx * instScale * r._fpx) / Math.max(dist, 0.05);
    var sy = (hy * instScale * r._fpx) / Math.max(dist, 0.05);
    if (sx < 0.6 || sy < 0.6) continue;
    if (p[0] < -sx || p[0] > W + sx || p[1] < -sy || p[1] > H + sy) continue;
    if (cArr) {
      var rr = base[0] * cArr[i * 3];
      var gg = base[1] * cArr[i * 3 + 1];
      var bb2 = base[2] * cArr[i * 3 + 2];
      var lit = __xtSwLightAt(r._lights, wx, wy, wz, 0, 1, 0);
      ctx.fillStyle = __xtSwCssQuant(rr * lit[0] + 0.004, gg * lit[1] + 0.006, bb2 * lit[2] + 0.012);
    } else {
      ctx.fillStyle = cls;
    }
    ctx.beginPath();
    ctx.ellipse(p[0], p[1], sx, sy, 0, 0, __xtSwTAU);
    ctx.fill();
  }
  ctx.globalAlpha = 1;
}

function __xtSwMaxScaleFromArray(a, o) {
  var sx = Math.sqrt(a[o] * a[o] + a[o + 1] * a[o + 1] + a[o + 2] * a[o + 2]);
  var sy = Math.sqrt(a[o + 4] * a[o + 4] + a[o + 5] * a[o + 5] + a[o + 6] * a[o + 6]);
  var sz = Math.sqrt(a[o + 8] * a[o + 8] + a[o + 9] * a[o + 9] + a[o + 10] * a[o + 10]);
  return Math.max(sx, Math.max(sy, sz));
}

/* -- fallback: flat-shaded triangles ------------------------------------- */

function __xtSwDrawFallbackMesh(ctx, obj, mat, geom, r, W, H) {
  var pos = geom.attributes && geom.attributes.position;
  if (!pos) return;
  var arr = pos.array;
  var idx = geom.index ? geom.index.array : null;
  var total = idx ? idx.length : pos.count;
  var m = obj.matrixWorld;
  var e = m.elements;
  var a = [0, 0, 0], b = [0, 0, 0], c = [0, 0, 0];
  var pa = [0, 0, 0, 0, 0, 0, 0, 0, 0];
  var alpha = mat.transparent ? (mat.opacity === undefined ? 1 : mat.opacity) : 1;
  var limit = Math.min(total, 6000);
  ctx.globalAlpha = alpha;
  for (var i = 0; i + 2 < limit; i += 3) {
    var ia = idx ? idx[i] : i;
    var ib = idx ? idx[i + 1] : i + 1;
    var ic = idx ? idx[i + 2] : i + 2;
    __xtSwXform(m, arr[ia * 3], arr[ia * 3 + 1], arr[ia * 3 + 2], a, 0);
    __xtSwXform(m, arr[ib * 3], arr[ib * 3 + 1], arr[ib * 3 + 2], b, 0);
    __xtSwXform(m, arr[ic * 3], arr[ic * 3 + 1], arr[ic * 3 + 2], c, 0);
    if (!__xtSwProjectTo(r._vp, a[0], a[1], a[2], W, H, pa, 0)) continue;
    if (!__xtSwProjectTo(r._vp, b[0], b[1], b[2], W, H, pa, 3)) continue;
    if (!__xtSwProjectTo(r._vp, c[0], c[1], c[2], W, H, pa, 6)) continue;
    var ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    var vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    var nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    var nl = Math.sqrt(nx * nx + ny * ny + nz * nz);
    if (nl < 0.000001) continue;
    nx /= nl; ny /= nl; nz /= nl;
    var cx = (a[0] + b[0] + c[0]) / 3, cy = (a[1] + b[1] + c[1]) / 3, cz = (a[2] + b[2] + c[2]) / 3;
    var vdx = r._camPos[0] - cx, vdy = r._camPos[1] - cy, vdz = r._camPos[2] - cz;
    if (nx * vdx + ny * vdy + nz * vdz <= 0) continue;
    var col = __xtSwBaseColor(mat, r._lights, cx, cy, cz, nx, ny, nz,
      __xtSwCamDist(r, cx, cy, cz));
    ctx.fillStyle = __xtSwCss(col[0], col[1], col[2]);
    ctx.beginPath();
    ctx.moveTo(pa[0], pa[1]);
    ctx.lineTo(pa[3], pa[4]);
    ctx.lineTo(pa[6], pa[7]);
    ctx.closePath();
    ctx.fill();
  }
  ctx.globalAlpha = 1;
}
