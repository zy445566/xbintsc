// Procedural low-poly geometry and a tiny mesh builder.
//
// Every visible object in the scene is built from a handful of primitives:
// boxes, spheres, cones/cylinders, tori and flat grids. Primitives are baked
// into one vertex/index buffer per animated group, with per-vertex colours, so
// the bike frame is one draw call and each wheel is one draw call.

import {
  PI,
  TAU,
  alignY,
  applyNormal,
  identity,
  multiply,
  normalMatrix3,
  scaling,
  transformPoint,
  translation,
} from "./math.js";

/* ----------------------------------------------------------------- colours */

/** `#rrggbb` -> linear-ish [r, g, b] in 0..1 (the renderer is stylised, not
 * physically based, so plain sRGB values are fine). */
export function hexColor(hex) {
  const value = hex.charAt(0) === "#" ? hex.slice(1) : hex;
  const n = parseInt(value, 16);
  return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
}

export function tint(colorValue, factor) {
  return [
    Math.min(1, colorValue[0] * factor),
    Math.min(1, colorValue[1] * factor),
    Math.min(1, colorValue[2] * factor),
  ];
}

export function mixColor(a, b, t) {
  return [
    a[0] + (b[0] - a[0]) * t,
    a[1] + (b[1] - a[1]) * t,
    a[2] + (b[2] - a[2]) * t,
  ];
}

export const WHITE = [1, 1, 1];

/* --------------------------------------------------------------- primitives */

/** Unit cube centred at the origin, one normal per face. */
export function box() {
  const positions = [];
  const normals = [];
  const indices = [];
  const faces = [
    [[-0.5, -0.5, 0.5], [0.5, -0.5, 0.5], [0.5, 0.5, 0.5], [-0.5, 0.5, 0.5], [0, 0, 1]],
    [[0.5, -0.5, -0.5], [-0.5, -0.5, -0.5], [-0.5, 0.5, -0.5], [0.5, 0.5, -0.5], [0, 0, -1]],
    [[0.5, -0.5, 0.5], [0.5, -0.5, -0.5], [0.5, 0.5, -0.5], [0.5, 0.5, 0.5], [1, 0, 0]],
    [[-0.5, -0.5, -0.5], [-0.5, -0.5, 0.5], [-0.5, 0.5, 0.5], [-0.5, 0.5, -0.5], [-1, 0, 0]],
    [[-0.5, 0.5, 0.5], [0.5, 0.5, 0.5], [0.5, 0.5, -0.5], [-0.5, 0.5, -0.5], [0, 1, 0]],
    [[-0.5, -0.5, -0.5], [0.5, -0.5, -0.5], [0.5, -0.5, 0.5], [-0.5, -0.5, 0.5], [0, -1, 0]],
  ];
  for (let f = 0; f < faces.length; f += 1) {
    const face = faces[f];
    const normal = face[4];
    const base = positions.length / 3;
    for (let i = 0; i < 4; i += 1) {
      const p = face[i];
      positions.push(p[0], p[1], p[2]);
      normals.push(normal[0], normal[1], normal[2]);
    }
    indices.push(base, base + 1, base + 2, base, base + 2, base + 3);
  }
  return pack(positions, normals, indices);
}

/** Unit sphere with smooth normals. */
export function sphere(latSegments, lonSegments) {
  const lat = latSegments === undefined ? 12 : latSegments;
  const lon = lonSegments === undefined ? 20 : lonSegments;
  const positions = [];
  const normals = [];
  const indices = [];
  for (let y = 0; y <= lat; y += 1) {
    const v = y / lat;
    const theta = v * PI;
    const sy = Math.cos(theta);
    const ring = Math.sin(theta);
    for (let x = 0; x <= lon; x += 1) {
      const u = x / lon;
      const phi = u * TAU;
      const sx = ring * Math.cos(phi);
      const sz = ring * Math.sin(phi);
      positions.push(sx, sy, sz);
      normals.push(sx, sy, sz);
    }
  }
  for (let y = 0; y < lat; y += 1) {
    for (let x = 0; x < lon; x += 1) {
      const a = y * (lon + 1) + x;
      const b = a + (lon + 1);
      indices.push(a, a + 1, b, a + 1, b + 1, b);
    }
  }
  return pack(positions, normals, indices);
}

/** Truncated cone / cylinder around the Y axis. Height 1, centred at origin. */
export function frustum(topRadius, bottomRadius, segments) {
  const count = segments === undefined ? 12 : segments;
  const positions = [];
  const normals = [];
  const indices = [];
  const slope = bottomRadius - topRadius;

  for (let i = 0; i <= count; i += 1) {
    const angle = (i / count) * TAU;
    const cx = Math.cos(angle);
    const sz = Math.sin(angle);
    let nx = cx;
    let ny = slope;
    let nz = sz;
    const nl = Math.sqrt(nx * nx + ny * ny + nz * nz);
    nx /= nl;
    ny /= nl;
    nz /= nl;
    positions.push(cx * topRadius, 0.5, sz * topRadius);
    normals.push(nx, ny, nz);
    positions.push(cx * bottomRadius, -0.5, sz * bottomRadius);
    normals.push(nx, ny, nz);
  }
  for (let i = 0; i < count; i += 1) {
    const base = i * 2;
    indices.push(base, base + 2, base + 1, base + 1, base + 2, base + 3);
  }

  if (topRadius > 0.00001) {
    const center = positions.length / 3;
    positions.push(0, 0.5, 0);
    normals.push(0, 1, 0);
    for (let i = 0; i <= count; i += 1) {
      const angle = (i / count) * TAU;
      positions.push(Math.cos(angle) * topRadius, 0.5, Math.sin(angle) * topRadius);
      normals.push(0, 1, 0);
    }
    for (let i = 0; i < count; i += 1) indices.push(center, center + 1 + i, center + 2 + i);
  }
  if (bottomRadius > 0.00001) {
    const center = positions.length / 3;
    positions.push(0, -0.5, 0);
    normals.push(0, -1, 0);
    for (let i = 0; i <= count; i += 1) {
      const angle = (i / count) * TAU;
      positions.push(Math.cos(angle) * bottomRadius, -0.5, Math.sin(angle) * bottomRadius);
      normals.push(0, -1, 0);
    }
    for (let i = 0; i < count; i += 1) indices.push(center, center + 2 + i, center + 1 + i);
  }
  return pack(positions, normals, indices);
}

export function cylinder(segments) {
  return frustum(1, 1, segments === undefined ? 12 : segments);
}

/** Torus in the XY plane, axle along Z (the wheel orientation). */
export function torus(majorRadius, minorRadius, majorSegments, minorSegments) {
  const major = majorSegments === undefined ? 28 : majorSegments;
  const minor = minorSegments === undefined ? 8 : minorSegments;
  const positions = [];
  const normals = [];
  const indices = [];
  for (let i = 0; i <= major; i += 1) {
    const u = (i / major) * TAU;
    const cu = Math.cos(u);
    const su = Math.sin(u);
    for (let j = 0; j <= minor; j += 1) {
      const v = (j / minor) * TAU;
      const cv = Math.cos(v);
      const sv = Math.sin(v);
      const r = majorRadius + minorRadius * cv;
      positions.push(r * cu, r * su, minorRadius * sv);
      normals.push(cv * cu, cv * su, sv);
    }
  }
  for (let i = 0; i < major; i += 1) {
    for (let j = 0; j < minor; j += 1) {
      const a = i * (minor + 1) + j;
      const b = a + (minor + 1);
      indices.push(a, b, a + 1, a + 1, b, b + 1);
    }
  }
  return pack(positions, normals, indices);
}

/** Unit disc in the XZ plane with an up normal (used for the soft shadow). */
export function discXZ(segments) {
  const count = segments === undefined ? 32 : segments;
  const positions = [0, 0, 0];
  const normals = [0, 1, 0];
  const indices = [];
  for (let i = 0; i <= count; i += 1) {
    const angle = (i / count) * TAU;
    positions.push(Math.cos(angle), 0, Math.sin(angle));
    normals.push(0, 1, 0);
  }
  for (let i = 0; i < count; i += 1) indices.push(0, i + 1, i + 2);
  return pack(positions, normals, indices);
}

/**
 * A flat grid in the XZ plane, one quad per cell, with a per-cell colour.
 * Used for the grass field and the asphalt, where a single flat colour would
 * look too clean.
 */
export function gridXZ(width, depth, columns, rows, colorAt) {
  const positions = [];
  const normals = [];
  const colors = [];
  const indices = [];
  const cellW = width / columns;
  const cellD = depth / rows;
  for (let row = 0; row < rows; row += 1) {
    for (let column = 0; column < columns; column += 1) {
      const x0 = -width / 2 + column * cellW;
      const z0 = -depth / 2 + row * cellD;
      const x1 = x0 + cellW;
      const z1 = z0 + cellD;
      const c = colorAt(column, row);
      const base = positions.length / 3;
      const corners = [x0, z0, x1, z0, x1, z1, x0, z1];
      for (let i = 0; i < 4; i += 1) {
        positions.push(corners[i * 2], 0, corners[i * 2 + 1]);
        normals.push(0, 1, 0);
        colors.push(c[0], c[1], c[2]);
      }
      indices.push(base, base + 1, base + 2, base, base + 2, base + 3);
    }
  }
  return {
    positions: new Float32Array(positions),
    normals: new Float32Array(normals),
    colors: new Float32Array(colors),
    indices: new Uint16Array(indices),
  };
}

function pack(positions, normals, indices) {
  return {
    positions: new Float32Array(positions),
    normals: new Float32Array(normals),
    indices: new Uint16Array(indices),
  };
}

/* --------------------------------------------------------------- mesh builder */

/**
 * Bake transformed primitives into one static mesh. Colour is per vertex, so
 * a whole group (a wheel, the bird, a tree) is a single draw call.
 */
export class MeshBuilder {
  constructor() {
    this.positions = [];
    this.normals = [];
    this.colors = [];
    this.indices = [];
  }

  add(geometry, matrix, color) {
    const normal = normalMatrix3(matrix);
    const colors = geometry.colors;
    const base = this.positions.length / 3;
    const count = geometry.positions.length / 3;
    for (let i = 0; i < count; i += 1) {
      const p = transformPoint(
        matrix,
        geometry.positions[i * 3],
        geometry.positions[i * 3 + 1],
        geometry.positions[i * 3 + 2],
      );
      this.positions.push(p[0], p[1], p[2]);
      const n = applyNormal(
        normal,
        geometry.normals[i * 3],
        geometry.normals[i * 3 + 1],
        geometry.normals[i * 3 + 2],
      );
      this.normals.push(n[0], n[1], n[2]);
      let c = color;
      if (c === undefined) {
        if (colors === undefined) {
          c = WHITE;
        } else {
          c = [colors[i * 3], colors[i * 3 + 1], colors[i * 3 + 2]];
        }
      }
      this.colors.push(c[0], c[1], c[2]);
    }
    for (let i = 0; i < geometry.indices.length; i += 1) {
      this.indices.push(base + geometry.indices[i]);
    }
  }

  addColored(geometry, matrix) {
    this.add(geometry, matrix, undefined);
  }

  addEllipsoid(center, rx, ry, rz, color, rotation) {
    const rot = rotation === undefined ? identity() : rotation;
    const matrix = multiply(
      translation(center[0], center[1], center[2]),
      multiply(rot, scaling(rx, ry, rz)),
    );
    this.add(sphere(12, 20), matrix, color);
  }

  addBox(center, sx, sy, sz, color, rotation) {
    const rot = rotation === undefined ? identity() : rotation;
    const matrix = multiply(
      translation(center[0], center[1], center[2]),
      multiply(rot, scaling(sx, sy, sz)),
    );
    this.add(box(), matrix, color);
  }

  addTube(a, b, radius, color, segments) {
    const dx = b[0] - a[0];
    const dy = b[1] - a[1];
    const dz = b[2] - a[2];
    const length = Math.sqrt(dx * dx + dy * dy + dz * dz);
    if (!(length > 0.00001)) return;
    const direction = [dx / length, dy / length, dz / length];
    const matrix = multiply(
      translation((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2),
      multiply(alignY(direction), scaling(radius, length, radius)),
    );
    this.add(cylinder(segments === undefined ? 8 : segments), matrix, color);
  }

  addTorus(center, majorRadius, minorRadius, color, rotation) {
    const rot = rotation === undefined ? identity() : rotation;
    const matrix = multiply(translation(center[0], center[1], center[2]), rot);
    this.add(torus(majorRadius, minorRadius, 24, 7), matrix, color);
  }

  build() {
    const count = this.positions.length / 3;
    if (count > 65535) {
      return {
        positions: new Float32Array(this.positions),
        normals: new Float32Array(this.normals),
        colors: new Float32Array(this.colors),
        indices: new Uint32Array(this.indices),
      };
    }
    return {
      positions: new Float32Array(this.positions),
      normals: new Float32Array(this.normals),
      colors: new Float32Array(this.colors),
      indices: new Uint16Array(this.indices),
    };
  }
}
