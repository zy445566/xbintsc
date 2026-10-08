#version 450

/* xbintsc GUI engine — rounded-rectangle fill, Vulkan/SPIR-V path.
 *
 * The descriptor layout matches what SDL_GPU's Vulkan backend builds
 * (`SDL_gpu_vulkan.c`, `CreateDescriptorSetLayout`): uniforms at
 * `set=1, binding=0`, and (in the text/image shaders) samplers at
 * `set=2, binding=0` with textures at `set=2, binding=1`.
 *
 * Vertex attributes (matching SDL_GPUVertexAttribute.location):
 *   0 -> position (pixels, viewport-relative)
 *   1 -> local    (pixels, relative to the rectangle centre)
 *   2 -> halfsize (rectangle half extents, pixels)
 *   3 -> radius   (pixels)
 *   4 -> colour
 * The push constant is the viewport size in logical pixels.
 */

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_local;
layout(location = 2) in vec2 in_halfsize;
layout(location = 3) in float in_radius;
layout(location = 4) in vec4 in_color;

layout(location = 0) out vec2 v_local;
layout(location = 1) out vec2 v_halfsize;
layout(location = 2) out float v_radius;
layout(location = 3) out vec4 v_color;

layout(set = 1, binding = 0) uniform Viewport {
    vec2 size;
} viewport;

void main() {
    vec2 ndc = vec2(in_position.x / viewport.size.x * 2.0 - 1.0,
                    1.0 - in_position.y / viewport.size.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_local = in_local;
    v_halfsize = in_halfsize;
    v_radius = in_radius;
    v_color = in_color;
}
