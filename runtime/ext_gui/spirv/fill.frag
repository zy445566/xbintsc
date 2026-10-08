#version 450

/* xbintsc GUI engine — rounded-rectangle fill fragment shader (Vulkan/SPIR-V).
 * The distance field mirrors the MSL/DXIL fill shaders. */

layout(location = 0) in vec2 v_local;
layout(location = 1) in vec2 v_halfsize;
layout(location = 2) in float v_radius;
layout(location = 3) in vec4 v_color;

layout(location = 0) out vec4 out_color;

void main() {
    vec2 d = abs(v_local) - v_halfsize + v_radius;
    float dist = min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - v_radius;
    float aa = max(fwidth(dist), 0.0001);
    float alpha = clamp(0.5 - dist / aa, 0.0, 1.0);
    out_color = vec4(v_color.rgb, v_color.a * alpha);
}
