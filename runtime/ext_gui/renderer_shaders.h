/*
 * xbintsc GUI engine — SDL_GPU Metal (MSL) shader sources.
 *
 * Shared by `renderer.cpp` (fills), `renderer_text.cpp` (glyphs) and
 * `renderer_image.cpp` (images). Text and image quads reuse `kTextMSL`; only
 * the fragment entry point differs (`fs_text` vs `fs_image`).
 */
#ifndef XT_GUI_RENDERER_SHADERS_H
#define XT_GUI_RENDERER_SHADERS_H

/* Rounded-rectangle fill. Vertex attributes (matching SDL_GPUVertexAttribute):
 *   0 -> position (pixels, viewport-relative)
 *   1 -> local   (pixels, relative to the rectangle centre)
 *   2 -> half    (rectangle half extents, pixels)
 *   3 -> radius  (pixels)
 *   4 -> colour
 * The push constant is the viewport size in logical pixels. */
inline constexpr const char *kFillMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct FillIn {
    float2 position [[attribute(0)]];
    float2 local    [[attribute(1)]];
    float2 halfsize [[attribute(2)]];
    float  radius   [[attribute(3)]];
    float4 color    [[attribute(4)]];
};

struct FillOut {
    float4 position [[position]];
    float2 local;
    float2 halfsize;
    float  radius;
    float4 color;
};

vertex FillOut vs_main(FillIn in [[stage_in]],
                       constant float2 &viewport [[buffer(0)]]) {
    FillOut out;
    float2 ndc = float2(in.position.x / viewport.x * 2.0 - 1.0,
                        1.0 - in.position.y / viewport.y * 2.0);
    out.position = float4(ndc, 0.0, 1.0);
    out.local = in.local;
    out.halfsize = in.halfsize;
    out.radius = in.radius;
    out.color = in.color;
    return out;
}

fragment float4 fs_main(FillOut in [[stage_in]]) {
    float2 d = abs(in.local) - in.halfsize + in.radius;
    float dist = min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - in.radius;
    float aa = max(fwidth(dist), 0.0001);
    float alpha = clamp(0.5 - dist / aa, 0.0, 1.0);
    return float4(in.color.rgb, in.color.a * alpha);
}
)MSL";

/* Glyph quads. Attributes:
 *   0 -> position (pixels, viewport-relative)
 *   1 -> uv (atlas coordinates in [0,1])
 *   2 -> colour
 * The fragment shader samples the single-channel atlas and modulates alpha. */
inline constexpr const char *kTextMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct TextIn {
    float2 position [[attribute(0)]];
    float2 uv       [[attribute(1)]];
    float4 color    [[attribute(2)]];
};

struct TextOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex TextOut vs_text(TextIn in [[stage_in]],
                       constant float2 &viewport [[buffer(0)]]) {
    TextOut out;
    float2 ndc = float2(in.position.x / viewport.x * 2.0 - 1.0,
                        1.0 - in.position.y / viewport.y * 2.0);
    out.position = float4(ndc, 0.0, 1.0);
    out.uv = in.uv;
    out.color = in.color;
    return out;
}

fragment float4 fs_text(TextOut in [[stage_in]],
                        texture2d<float> atlas [[texture(0)]],
                        sampler samp [[sampler(0)]]) {
    float coverage = atlas.sample(samp, in.uv).r;
    return float4(in.color.rgb, in.color.a * coverage);
}

/* Image quads reuse the text vertex layout but sample an RGBA texture. */
fragment float4 fs_image(TextOut in [[stage_in]],
                         texture2d<float> tex [[texture(0)]],
                         sampler samp [[sampler(0)]]) {
    float4 c = tex.sample(samp, in.uv);
    return float4(c.rgb * in.color.rgb, c.a * in.color.a);
}
)MSL";

#endif /* XT_GUI_RENDERER_SHADERS_H */
