#version 450

/* xbintsc GUI engine — glyph quads (Vulkan/SPIR-V): sample the single-channel
 * atlas and modulate alpha by its coverage. */

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(location = 0) out vec4 out_color;

layout(set = 2, binding = 0) uniform sampler2D atlas;

void main() {
    float coverage = texture(atlas, v_uv).r;
    out_color = vec4(v_color.rgb, v_color.a * coverage);
}
