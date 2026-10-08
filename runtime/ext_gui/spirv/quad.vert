#version 450

/* xbintsc GUI engine — glyph and image quads, Vulkan/SPIR-V path.
 *
 * The vertex stage is shared by both: the fragment stage differs only in how it
 * uses the sampled texture (`fs_text` reads the atlas' red channel, `fs_image`
 * modulates an RGBA texture). Attributes:
 *   0 -> position (pixels, viewport-relative)
 *   1 -> uv (atlas coordinates in [0,1])
 *   2 -> colour
 */

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

layout(set = 1, binding = 0) uniform Viewport {
    vec2 size;
} viewport;

void main() {
    vec2 ndc = vec2(in_position.x / viewport.size.x * 2.0 - 1.0,
                    1.0 - in_position.y / viewport.size.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = in_uv;
    v_color = in_color;
}
