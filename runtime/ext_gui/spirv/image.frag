#version 450

/* xbintsc GUI engine — image quads (Vulkan/SPIR-V): sample an RGBA texture and
 * modulate it by the vertex colour. */

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(location = 0) out vec4 out_color;

layout(set = 2, binding = 0) uniform sampler2D image;

void main() {
    vec4 c = texture(image, v_uv);
    out_color = vec4(c.rgb * v_color.rgb, c.a * v_color.a);
}
