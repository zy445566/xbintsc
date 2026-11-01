// Small, dependency-free 4x4 matrix / vector math for the WebGL 2 scene.
//
// Matrices are column-major, exactly as WebGL expects them, so a Float32Array
// can be handed to `uniformMatrix4fv` without a transpose. Only the handful of
// operations the demo needs are implemented; this keeps the whole example
// readable and gives the compiler no exotic syntax to deal with.

export const PI = Math.PI;
export const TAU = Math.PI * 2;

export function identity(out) {
  const m = out === undefined ? new Float32Array(16) : out;
  for (let i = 0; i < 16; i += 1) m[i] = 0;
  m[0] = 1;
  m[5] = 1;
  m[10] = 1;
  m[15] = 1;
  return m;
}

/** `out = a * b` (both column-major). `out` must not alias `a` or `b`. */
export function multiply(a, b, out) {
  const m = out === undefined ? new Float32Array(16) : out;
  for (let column = 0; column < 4; column += 1) {
    for (let row = 0; row < 4; row += 1) {
      let sum = 0;
      for (let k = 0; k < 4; k += 1) {
        sum += a[k * 4 + row] * b[column * 4 + k];
      }
      m[column * 4 + row] = sum;
    }
  }
  return m;
}

/** Chain any number of matrices from left (outermost) to right (innermost). */
export function chain() {
  let result = identity();
  for (let i = 0; i < arguments.length; i += 1) {
    result = multiply(result, arguments[i]);
  }
  return result;
}

export function translation(x, y, z, out) {
  const m = identity(out);
  m[12] = x;
  m[13] = y;
  m[14] = z;
  return m;
}

export function scaling(x, y, z, out) {
  const m = identity(out);
  m[0] = x;
  m[5] = y;
  m[10] = z;
  return m;
}

export function rotationX(angle, out) {
  const m = identity(out);
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  m[5] = c;
  m[6] = s;
  m[9] = -s;
  m[10] = c;
  return m;
}

export function rotationY(angle, out) {
  const m = identity(out);
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  m[0] = c;
  m[2] = -s;
  m[8] = s;
  m[10] = c;
  return m;
}

export function rotationZ(angle, out) {
  const m = identity(out);
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  m[0] = c;
  m[1] = s;
  m[4] = -s;
  m[5] = c;
  return m;
}

/** Standard right-handed perspective projection. */
export function perspective(fovY, aspect, near, far, out) {
  const m = identity(out);
  const f = 1 / Math.tan(fovY / 2);
  m[0] = f / aspect;
  m[5] = f;
  m[10] = (far + near) / (near - far);
  m[11] = -1;
  m[14] = (2 * far * near) / (near - far);
  m[15] = 0;
  return m;
}

export function lookAt(eye, target, up, out) {
  const zx = eye[0] - target[0];
  const zy = eye[1] - target[1];
  const zz = eye[2] - target[2];
  let zl = Math.sqrt(zx * zx + zy * zy + zz * zz);
  if (!(zl > 0.00001)) zl = 1;
  const z0 = zx / zl;
  const z1 = zy / zl;
  const z2 = zz / zl;

  let x0 = up[1] * z2 - up[2] * z1;
  let x1 = up[2] * z0 - up[0] * z2;
  let x2 = up[0] * z1 - up[1] * z0;
  let xl = Math.sqrt(x0 * x0 + x1 * x1 + x2 * x2);
  if (!(xl > 0.00001)) {
    /* `up` is parallel to the view direction: pick another reference axis. */
    x0 = 1;
    x1 = 0;
    x2 = 0;
    xl = 1;
  }
  x0 /= xl;
  x1 /= xl;
  x2 /= xl;

  const y0 = z1 * x2 - z2 * x1;
  const y1 = z2 * x0 - z0 * x2;
  const y2 = z0 * x1 - z1 * x0;

  const m = identity(out);
  m[0] = x0;
  m[1] = y0;
  m[2] = z0;
  m[4] = x1;
  m[5] = y1;
  m[6] = z1;
  m[8] = x2;
  m[9] = y2;
  m[10] = z2;
  m[12] = -(x0 * eye[0] + x1 * eye[1] + x2 * eye[2]);
  m[13] = -(y0 * eye[0] + y1 * eye[1] + y2 * eye[2]);
  m[14] = -(z0 * eye[0] + z1 * eye[1] + z2 * eye[2]);
  return m;
}

/**
 * A rotation whose local +Y axis points along `direction` (used to place a
 * unit cylinder between two joints). The other two axes are chosen from a
 * stable reference so repeated calls do not twist.
 */
export function alignY(direction, out) {
  const dx = direction[0];
  const dy = direction[1];
  const dz = direction[2];
  let len = Math.sqrt(dx * dx + dy * dy + dz * dz);
  if (!(len > 0.00001)) return identity(out);
  const y0 = dx / len;
  const y1 = dy / len;
  const y2 = dz / len;

  let rx = 0;
  let ry = 1;
  let rz = 0;
  if (Math.abs(y1) > 0.98) {
    rx = 0;
    ry = 0;
    rz = 1;
  }

  let x0 = ry * y2 - rz * y1;
  let x1 = rz * y0 - rx * y2;
  let x2 = rx * y1 - ry * y0;
  let xl = Math.sqrt(x0 * x0 + x1 * x1 + x2 * x2);
  if (!(xl > 0.00001)) {
    x0 = 1;
    x1 = 0;
    x2 = 0;
    xl = 1;
  }
  x0 /= xl;
  x1 /= xl;
  x2 /= xl;

  const z0 = x1 * y2 - x2 * y1;
  const z1 = x2 * y0 - x0 * y2;
  const z2 = x0 * y1 - x1 * y0;

  const m = identity(out);
  m[0] = x0;
  m[1] = x1;
  m[2] = x2;
  m[4] = y0;
  m[5] = y1;
  m[6] = y2;
  m[8] = z0;
  m[9] = z1;
  m[10] = z2;
  return m;
}

/** `T(p) * rotation * T(-p)`: rotate around a pivot. */
export function pivot(px, py, pz, rotation, out) {
  const toPivot = translation(px, py, pz);
  const back = translation(-px, -py, -pz);
  return multiply(multiply(toPivot, rotation), back, out);
}

/**
 * Inverse-transpose of the upper-left 3x3, in row-major order. The mesh
 * builder uses it to keep normals correct under non-uniform scales.
 */
export function normalMatrix3(m) {
  const a = m[0];
  const b = m[4];
  const c = m[8];
  const d = m[1];
  const e = m[5];
  const f = m[9];
  const g = m[2];
  const h = m[6];
  const i = m[10];

  const det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (Math.abs(det) < 0.0000001) return [1, 0, 0, 0, 1, 0, 0, 0, 1];
  const inv = 1 / det;

  /* Rows of the inverse. */
  const ia = (e * i - f * h) * inv;
  const ib = (c * h - b * i) * inv;
  const ic = (b * f - c * e) * inv;
  const id = (f * g - d * i) * inv;
  const ie = (a * i - c * g) * inv;
  const iff = (c * d - a * f) * inv;
  const ig = (d * h - e * g) * inv;
  const ih = (b * g - a * h) * inv;
  const ii = (a * e - b * d) * inv;

  /* Transpose: the cofactor matrix itself, laid out in rows. */
  return [ia, id, ig, ib, ie, ih, ic, iff, ii];
}

export function transformPoint(m, x, y, z) {
  return [
    m[0] * x + m[4] * y + m[8] * z + m[12],
    m[1] * x + m[5] * y + m[9] * z + m[13],
    m[2] * x + m[6] * y + m[10] * z + m[14],
  ];
}

export function applyNormal(n, x, y, z) {
  const nx = n[0] * x + n[1] * y + n[2] * z;
  const ny = n[3] * x + n[4] * y + n[5] * z;
  const nz = n[6] * x + n[7] * y + n[8] * z;
  const len = Math.sqrt(nx * nx + ny * ny + nz * nz);
  if (!(len > 0.00001)) return [0, 1, 0];
  return [nx / len, ny / len, nz / len];
}

export function clamp(value, low, high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

export function mix(a, b, t) {
  return a + (b - a) * t;
}

export function smoothstep(low, high, value) {
  if (high === low) return value < low ? 0 : 1;
  let t = (value - low) / (high - low);
  t = clamp(t, 0, 1);
  return t * t * (3 - 2 * t);
}

export function distance3(a, b) {
  const dx = a[0] - b[0];
  const dy = a[1] - b[1];
  const dz = a[2] - b[2];
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}
