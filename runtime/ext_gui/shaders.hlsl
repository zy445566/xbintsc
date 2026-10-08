/*
 * xbintsc GUI engine — shader sources compiled ahead of time into the embedded
 * blobs in `renderer_blobs.h`.
 *
 * Three programs, two entry points each:
 *   - `vs_main` / `fs_main`   rounded-rectangle fills
 *   - `vs_text` / `fs_text`   glyph quads (single-channel atlas)
 *   - `vs_text` / `fs_image`  image quads (RGBA texture)
 *
 * `vs_main` and `vs_text` are identical in effect; they are kept separate so the
 * Metal (MSL) path keeps its distinct entry-point names, and this file is the
 * HLSL half of the pair. Vertex attribute semantics map 1:1 onto
 * `SDL_GPUVertexAttribute.location`, and the push constant becomes `b0`.
 *
 * Compile with (Windows SDK `dxc`):
 *   dxc -T vs_6_0 -E vs_main  -Fo fill_vs.dxil  shaders.hlsl
 *   dxc -T ps_6_0 -E fs_main  -Fo fill_fs.dxil  shaders.hlsl
 *   dxc -T vs_6_0 -E vs_text  -Fo text_vs.dxil  shaders.hlsl
 *   dxc -T ps_6_0 -E fs_text  -Fo text_fs.dxil  shaders.hlsl
 *   dxc -T ps_6_0 -E fs_image -Fo image_fs.dxil shaders.hlsl
 */

cbuffer Viewport : register(b0) { float2 viewport; };

/* -- fills ---------------------------------------------------------------- */

struct FillIn {
    float2 position : TEXCOORD0;
    float2 local    : TEXCOORD1;
    float2 halfsize : TEXCOORD2;
    float  radius   : TEXCOORD3;
    float4 color    : TEXCOORD4;
};

struct FillOut {
    float4 position : SV_Position;
    float2 local    : TEXCOORD1;
    float2 halfsize : TEXCOORD2;
    float  radius   : TEXCOORD3;
    float4 color    : TEXCOORD4;
};

FillOut vs_main(FillIn input) {
    FillOut output;
    float2 ndc = float2(input.position.x / viewport.x * 2.0 - 1.0,
                        1.0 - input.position.y / viewport.y * 2.0);
    output.position = float4(ndc, 0.0, 1.0);
    output.local = input.local;
    output.halfsize = input.halfsize;
    output.radius = input.radius;
    output.color = input.color;
    return output;
}

float4 fs_main(FillOut input) : SV_Target {
    float2 d = abs(input.local) - input.halfsize + input.radius;
    float dist = min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - input.radius;
    float aa = max(fwidth(dist), 0.0001);
    float alpha = clamp(0.5 - dist / aa, 0.0, 1.0);
    return float4(input.color.rgb, input.color.a * alpha);
}

/* -- text and images ------------------------------------------------------ */

struct TextIn {
    float2 position : TEXCOORD0;
    float2 uv       : TEXCOORD1;
    float4 color    : TEXCOORD2;
};

struct TextOut {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD1;
    float4 color    : TEXCOORD2;
};

Texture2D    atlas : register(t0);
SamplerState samp  : register(s0);

TextOut vs_text(TextIn input) {
    TextOut output;
    float2 ndc = float2(input.position.x / viewport.x * 2.0 - 1.0,
                        1.0 - input.position.y / viewport.y * 2.0);
    output.position = float4(ndc, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 fs_text(TextOut input) : SV_Target {
    float coverage = atlas.Sample(samp, input.uv).r;
    return float4(input.color.rgb, input.color.a * coverage);
}

float4 fs_image(TextOut input) : SV_Target {
    float4 c = atlas.Sample(samp, input.uv);
    return float4(c.rgb * input.color.rgb, c.a * input.color.a);
}
