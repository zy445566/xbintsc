// GLSL ES 3.00 shader sources. These are ordinary WebGL 2 shaders: the gui
// extension's shader bridge translates them to SPIR-V (or MSL on Metal), so the
// same strings also run unchanged in a browser.

export const LIT_VERTEX = `#version 300 es
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 a_color;

uniform mat4 u_view_proj;
uniform mat4 u_model;
uniform vec3 u_normal_scale;

out vec3 v_normal;
out vec3 v_color;
out vec3 v_world;

void main() {
  vec4 world = u_model * vec4(a_position, 1.0);
  v_world = world.xyz;
  v_normal = normalize(u_normal_scale * mat3(u_model) * a_normal);
  v_color = a_color;
  gl_Position = u_view_proj * world;
}
`;

export const LIT_FRAGMENT = `#version 300 es
precision highp float;

in vec3 v_normal;
in vec3 v_color;
in vec3 v_world;

uniform vec3 u_light_dir;
uniform vec3 u_camera;
uniform vec3 u_fog_color;
uniform float u_fog_density;
uniform vec3 u_tint;
uniform float u_alpha;

out vec4 frag_color;

void main() {
  vec3 N = normalize(v_normal);
  vec3 L = normalize(u_light_dir);
  vec3 V = normalize(u_camera - v_world);

  float diffuse = max(dot(N, L), 0.0);
  float sky = max(N.y, 0.0);
  float fill = max(dot(N, -L), 0.0);
  float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);

  vec3 base = v_color * u_tint;
  vec3 sun = vec3(1.16, 1.02, 0.82);
  vec3 color = base * sun * (0.40 + 0.92 * diffuse);
  color += base * vec3(0.16, 0.24, 0.34) * (0.35 + 0.65 * sky);
  color += base * vec3(0.14, 0.10, 0.07) * fill;
  color += vec3(1.0, 0.68, 0.32) * rim * 0.32;
  color *= 1.12;

  float distance_to_camera = length(u_camera - v_world);
  float fog = 1.0 - exp(-distance_to_camera * u_fog_density);
  color = mix(color, u_fog_color, clamp(fog, 0.0, 0.82));

  color = color / (color + vec3(0.56));
  color = pow(color, vec3(0.88));
  float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
  color = mix(vec3(luma), color, 1.24);
  color = (color - 0.5) * 1.06 + 0.5;

  frag_color = vec4(clamp(color, 0.0, 1.0), u_alpha);
}
`;

/** One fullscreen quad; the sky is entirely procedural. */
export const SKY_VERTEX = `#version 300 es
layout(location = 0) in vec2 a_position;
out vec2 v_uv;

void main() {
  v_uv = a_position;
  gl_Position = vec4(a_position, 0.999, 1.0);
}
`;

export const SKY_FRAGMENT = `#version 300 es
precision highp float;

in vec2 v_uv;
uniform vec2 u_resolution;
uniform float u_time;

out vec4 frag_color;

void main() {
  float aspect = u_resolution.x / max(u_resolution.y, 1.0);
  vec2 p = vec2(v_uv.x * aspect, v_uv.y);
  float h = clamp(v_uv.y * 0.5 + 0.5, 0.0, 1.0);

  vec3 low = vec3(1.00, 0.56, 0.22);
  vec3 warm = vec3(1.00, 0.82, 0.45);
  vec3 mid = vec3(0.30, 0.66, 0.95);
  vec3 high = vec3(0.06, 0.32, 0.72);

  vec3 sky = mix(low, warm, smoothstep(0.00, 0.20, h));
  sky = mix(sky, mid, smoothstep(0.20, 0.56, h));
  sky = mix(sky, high, smoothstep(0.56, 1.00, h));

  vec2 sun = vec2(-0.10 * aspect, 0.46);
  vec2 d = p - sun;
  float glow = exp(-length(d * vec2(0.75, 1.15)) * 3.2);
  float disk = smoothstep(0.14, 0.105, length(d * vec2(0.92, 1.08)));

  sky += vec3(1.00, 0.58, 0.20) * glow * 0.80;
  sky += vec3(1.00, 0.95, 0.78) * disk * 1.55;

  float band = sin((v_uv.y + 0.2) * 18.0 + u_time * 0.035) * 0.006;
  sky += vec3(0.12, 0.08, 0.04) * band;

  float luma = dot(sky, vec3(0.2126, 0.7152, 0.0722));
  sky = mix(vec3(luma), sky, 1.22);

  frag_color = vec4(clamp(sky, 0.0, 1.0), 1.0);
}
`;

/** Instanced scenery: one mesh, an x/z/scale/y-offset per instance. */
export const INSTANCED_VERTEX = `#version 300 es
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 a_color;
layout(location = 3) in vec4 a_instance;

uniform mat4 u_view_proj;
uniform float u_scroll;
uniform float u_range;

out vec3 v_normal;
out vec3 v_color;
out vec3 v_world;

void main() {
  float span = max(u_range, 1.0);
  float x = mod(a_instance.x - u_scroll, span) - span * 0.5;
  float scale = a_instance.z;
  float base_y = a_instance.w;
  vec3 local = vec3(a_position.x * scale, a_position.y * scale, a_position.z * scale);
  vec3 world = vec3(x + local.x, base_y + local.y, a_instance.y + local.z);
  v_world = world;
  v_normal = normalize(a_normal);
  v_color = a_color;
  gl_Position = u_view_proj * vec4(world, 1.0);
}
`;

export const INSTANCED_FRAGMENT = `#version 300 es
precision highp float;

in vec3 v_normal;
in vec3 v_color;
in vec3 v_world;

uniform vec3 u_light_dir;
uniform vec3 u_camera;
uniform vec3 u_fog_color;
uniform float u_fog_density;

out vec4 frag_color;

void main() {
  vec3 N = normalize(v_normal);
  vec3 L = normalize(u_light_dir);
  float diffuse = max(dot(N, L), 0.0);
  float sky = max(N.y, 0.0);
  vec3 sun = vec3(1.16, 1.02, 0.82);
  vec3 color = v_color * sun * (0.40 + 0.90 * diffuse);
  color += v_color * vec3(0.16, 0.24, 0.34) * (0.35 + 0.65 * sky);
  color *= 1.12;

  float distance_to_camera = length(u_camera - v_world);
  float fog = 1.0 - exp(-distance_to_camera * u_fog_density);
  color = mix(color, u_fog_color, clamp(fog, 0.0, 0.82));
  color = color / (color + vec3(0.56));
  color = pow(color, vec3(0.88));
  float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
  color = mix(vec3(luma), color, 1.24);
  color = (color - 0.5) * 1.06 + 0.5;

  frag_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
`;

/** Soft blob shadow: a unit XZ disc with a radial alpha falloff. */
export const SHADOW_VERTEX = `#version 300 es
layout(location = 0) in vec3 a_position;

uniform mat4 u_view_proj;
uniform mat4 u_model;

out vec2 v_local;

void main() {
  v_local = a_position.xz;
  gl_Position = u_view_proj * u_model * vec4(a_position, 1.0);
}
`;

export const SHADOW_FRAGMENT = `#version 300 es
precision highp float;

in vec2 v_local;
uniform float u_alpha;

out vec4 frag_color;

void main() {
  float distance_from_center = length(v_local);
  float fade = 1.0 - smoothstep(0.15, 1.0, distance_from_center);
  frag_color = vec4(0.035, 0.045, 0.055, u_alpha * fade);
}
`;
