// WGSL sources for the WebGPU backend.
//
// This is the exact same scene as the GLSL ES 3.00 pair in shaders.js, written
// for `navigator.gpu`: one module holds all four pipelines (lit, instanced,
// sky and shadow), and the renderer picks an entry point per draw.
//
// The matrices come from math.js, which is an OpenGL-style projection (clip
// depth -1…1). WebGPU wants 0…1, so every vertex entry point maps
// z = (z + w) / 2 after the view-projection multiply. Everything else —
// per-vertex colour, the warm sun/fill/rim lighting, distance fog and the
// tone curve — is a direct translation of the GLSL fragment stage.

export const WGSL_SCENE = `
struct Uniforms {
  view_proj: mat4x4f,
  model: mat4x4f,
  light_dir: vec3f,
  camera: vec3f,
  fog_color: vec3f,
  tint: vec3f,
  normal_scale: vec3f,
  resolution: vec2f,
  fog_density: f32,
  alpha: f32,
  scroll: f32,
  range: f32,
  time: f32,
};

@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitOut {
  @builtin(position) position: vec4f,
  @location(0) normal: vec3f,
  @location(1) color: vec3f,
  @location(2) world: vec3f,
};

@vertex
fn lit_vertex(@location(0) position: vec3f, @location(1) normal: vec3f,
              @location(2) color: vec3f) -> LitOut {
  var world: vec4f = u.model * vec4f(position, 1.0);
  var clip: vec4f = u.view_proj * world;
  clip.z = (clip.z + clip.w) * 0.5;

  let basis = mat3x3f(u.model[0].xyz, u.model[1].xyz, u.model[2].xyz);
  var out: LitOut;
  out.position = clip;
  out.normal = normalize(u.normal_scale * (basis * normal));
  out.color = color;
  out.world = world.xyz;
  return out;
}

@fragment
fn lit_fragment(in: LitOut) -> @location(0) vec4f {
  let N = normalize(in.normal);
  let L = normalize(u.light_dir);
  let V = normalize(u.camera - in.world);

  let diffuse = max(dot(N, L), 0.0);
  let sky = max(N.y, 0.0);
  let fill = max(dot(N, -L), 0.0);
  let rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);

  let base = in.color * u.tint;
  let sun = vec3f(1.16, 1.02, 0.82);
  var color: vec3f = base * sun * (0.40 + 0.92 * diffuse);
  color = color + base * vec3f(0.16, 0.24, 0.34) * (0.35 + 0.65 * sky);
  color = color + base * vec3f(0.14, 0.10, 0.07) * fill;
  color = color + vec3f(1.0, 0.68, 0.32) * rim * 0.32;
  color = color * 1.12;

  let to_camera = length(u.camera - in.world);
  let fog = 1.0 - exp(-to_camera * u.fog_density);
  color = mix(color, u.fog_color, clamp(fog, 0.0, 0.82));

  color = color / (color + vec3f(0.56));
  color = pow(color, vec3f(0.88));
  let luma = dot(color, vec3f(0.2126, 0.7152, 0.0722));
  color = mix(vec3f(luma), color, 1.24);
  color = (color - 0.5) * 1.06 + 0.5;

  return vec4f(clamp(color, 0.0, 1.0), u.alpha);
}

@vertex
fn instanced_vertex(@location(0) position: vec3f, @location(1) normal: vec3f,
                    @location(2) color: vec3f,
                    @location(3) instance: vec4f) -> LitOut {
  let span = max(u.range, 1.0);
  let shifted = instance.x - u.scroll;
  let x = shifted - span * floor(shifted / span) - span * 0.5;
  let scale = instance.z;
  let base_y = instance.w;
  let local = vec3f(position.x * scale, position.y * scale, position.z * scale);
  let world = vec3f(x + local.x, base_y + local.y, instance.y + local.z);

  var clip: vec4f = u.view_proj * vec4f(world, 1.0);
  clip.z = (clip.z + clip.w) * 0.5;

  var out: LitOut;
  out.position = clip;
  out.normal = normalize(normal);
  out.color = color;
  out.world = world;
  return out;
}

@fragment
fn instanced_fragment(in: LitOut) -> @location(0) vec4f {
  let N = normalize(in.normal);
  let L = normalize(u.light_dir);
  let sun = vec3f(1.16, 1.02, 0.82);
  let sky = max(N.y, 0.0);

  var color: vec3f = in.color * sun * (0.40 + 0.90 * max(dot(N, L), 0.0));
  color = color + in.color * vec3f(0.16, 0.24, 0.34) * (0.35 + 0.65 * sky);
  color = color * 1.12;

  let to_camera = length(u.camera - in.world);
  let fog = 1.0 - exp(-to_camera * u.fog_density);
  color = mix(color, u.fog_color, clamp(fog, 0.0, 0.82));
  color = color / (color + vec3f(0.56));
  color = pow(color, vec3f(0.88));
  let luma = dot(color, vec3f(0.2126, 0.7152, 0.0722));
  color = mix(vec3f(luma), color, 1.24);
  color = (color - 0.5) * 1.06 + 0.5;

  return vec4f(clamp(color, 0.0, 1.0), 1.0);
}

struct ShadowOut {
  @builtin(position) position: vec4f,
  @location(0) local: vec2f,
};

@vertex
fn shadow_vertex(@location(0) position: vec3f) -> ShadowOut {
  var clip: vec4f = u.view_proj * u.model * vec4f(position, 1.0);
  clip.z = (clip.z + clip.w) * 0.5;

  var out: ShadowOut;
  out.position = clip;
  out.local = position.xz;
  return out;
}

@fragment
fn shadow_fragment(in: ShadowOut) -> @location(0) vec4f {
  let d = length(in.local);
  let fade = 1.0 - smoothstep(0.15, 1.0, d);
  return vec4f(0.035, 0.045, 0.055, u.alpha * fade);
}

struct SkyOut {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
};

@vertex
fn sky_vertex(@location(0) position: vec2f) -> SkyOut {
  var out: SkyOut;
  out.position = vec4f(position, 0.999, 1.0);
  out.uv = position;
  return out;
}

@fragment
fn sky_fragment(in: SkyOut) -> @location(0) vec4f {
  let aspect = u.resolution.x / max(u.resolution.y, 1.0);
  let p = vec2f(in.uv.x * aspect, in.uv.y);
  let h = clamp(in.uv.y * 0.5 + 0.5, 0.0, 1.0);

  let low = vec3f(1.00, 0.56, 0.22);
  let warm = vec3f(1.00, 0.82, 0.45);
  let mid = vec3f(0.30, 0.66, 0.95);
  let high = vec3f(0.06, 0.32, 0.72);

  var sky: vec3f = mix(low, warm, smoothstep(0.00, 0.20, h));
  sky = mix(sky, mid, smoothstep(0.20, 0.56, h));
  sky = mix(sky, high, smoothstep(0.56, 1.00, h));

  let sun = vec2f(-0.10 * aspect, 0.46);
  let d = p - sun;
  let glow = exp(-length(d * vec2f(0.75, 1.15)) * 3.2);
  let disk = 1.0 - smoothstep(0.105, 0.14, length(d * vec2f(0.92, 1.08)));

  sky = sky + vec3f(1.00, 0.58, 0.20) * glow * 0.80;
  sky = sky + vec3f(1.00, 0.95, 0.78) * disk * 1.55;

  let band = sin((in.uv.y + 0.2) * 18.0 + u.time * 0.035) * 0.006;
  sky = sky + vec3f(0.12, 0.08, 0.04) * band;

  let luma = dot(sky, vec3f(0.2126, 0.7152, 0.0722));
  sky = mix(vec3f(luma), sky, 1.22);

  return vec4f(clamp(sky, 0.0, 1.0), 1.0);
}
`;
