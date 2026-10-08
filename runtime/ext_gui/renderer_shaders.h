/*
 * xbintsc GUI engine — shader sources.
 *
 * `renderer.cpp` (fills), `renderer_text.cpp` (glyphs) and `renderer_image.cpp`
 * (images) each build one SDL_GPU graphics pipeline. Three shader families back
 * them, one per SDL_GPU backend:
 *
 *   - Metal: the MSL string literals below, which SDL_GPU compiles at runtime.
 *   - Vulkan: SPIR-V blobs in `renderer_shaders_data.h`, compiled from
 *     `spirv/*.{vert,frag}` by `scripts/build-gui-shaders.mjs`.
 *   - Direct3D 12: DXIL blobs from the same header, compiled from `shaders.hlsl`.
 *
 * `selectShader` resolves a logical program and stage to whichever of those the
 * device accepts. The backend rejects any other format, so a missing blob means
 * "no pipeline", never a mismatched one.
 *
 * Text and image quads share the vertex program; only the fragment entry point
 * differs (`fs_text` vs `fs_image`).
 *
 * Note the entry-point names differ per format: MSL/DXIL keep the descriptive
 * names from their sources, while glslc emits the GLSL `main`.
 */
#ifndef XT_GUI_RENDERER_SHADERS_H
#define XT_GUI_RENDERER_SHADERS_H

#include <SDL3/SDL.h>
#include <cstring>

#include "renderer_shaders_data.h"

namespace xtgui {
namespace renderer_detail {

/** The three pipeline programs the engine builds. */
enum class ShaderProgram { Fill, Text, Image };

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

/** One stage of one pipeline program, in whichever format the device accepts. */
struct ShaderSource {
  SDL_GPUShaderFormat format = 0;
  const unsigned char *code = nullptr;
  size_t size = 0;
  const char *entry = nullptr;

  bool valid() const { return code != nullptr; }
};

/** The MSL text backing `program` (one literal covers both stages). */
inline const char *mslSource(ShaderProgram program) {
  return program == ShaderProgram::Fill ? kFillMSL : kTextMSL;
}

/** MSL entry point names for `program`, matching the literals below. */
inline const char *vertexEntry(ShaderProgram program) {
  return program == ShaderProgram::Fill ? "vs_main" : "vs_text";
}

inline const char *fragmentEntry(ShaderProgram program) {
  if (program == ShaderProgram::Fill) return "fs_main";
  return program == ShaderProgram::Text ? "fs_text" : "fs_image";
}

/** A blob pair for one shader format: bytes plus the entry-point names it uses.
 * The names are stored in `ShaderSource` rather than resolved at the call site
 * because cross-compilers rename entry points (glslc emits `main`, dxc keeps the
 * HLSL name, MSL keeps the Metal name). */
struct ShaderBlobs {
  const unsigned char *vertex;
  size_t vertex_size;
  const char *vertex_entry;
  const unsigned char *fragment;
  size_t fragment_size;
  const char *fragment_entry;
};

/** SPIR-V blobs for `program` (Vulkan). glslc names every entry point `main`. */
inline ShaderBlobs spirvBlobs(ShaderProgram program) {
  switch (program) {
    case ShaderProgram::Fill:
      return {kFillSpirvVs, kFillSpirvVsSize, "main", kFillSpirvFs, kFillSpirvFsSize, "main"};
    case ShaderProgram::Text:
      return {kTextSpirvVs, kTextSpirvVsSize, "main", kTextSpirvFs, kTextSpirvFsSize, "main"};
    case ShaderProgram::Image:
      return {kTextSpirvVs, kTextSpirvVsSize, "main", kImageSpirvFs, kImageSpirvFsSize, "main"};
  }
  return {nullptr, 0, nullptr, nullptr, 0, nullptr};
}

/** DXIL blobs for `program` (Direct3D 12); one blob per entry point. */
inline ShaderBlobs dxilBlobs(ShaderProgram program) {
  switch (program) {
    case ShaderProgram::Fill:
      return {kFillDxilVs, kFillDxilVsSize, "vs_main", kFillDxilFs, kFillDxilFsSize, "fs_main"};
    case ShaderProgram::Text:
      return {kTextDxilVs, kTextDxilVsSize, "vs_text", kTextDxilFs, kTextDxilFsSize, "fs_text"};
    case ShaderProgram::Image:
      return {kTextDxilVs, kTextDxilVsSize, "vs_text", kImageDxilFs, kImageDxilFsSize, "fs_image"};
  }
  return {nullptr, 0, nullptr, nullptr, 0, nullptr};
}

/** MSL for `program`: a single source holds both stages, so it is expressed as a
 * blob pair sharing one buffer. */
inline ShaderBlobs mslBlobs(ShaderProgram program) {
  const char *msl = mslSource(program);
  const size_t size = strlen(msl);
  const unsigned char *code = (const unsigned char *)msl;
  return {code, size, vertexEntry(program), code, size, fragmentEntry(program)};
}

/** Resolve `program`'s vertex and fragment stages to shaders the device accepts.
 * Returns an invalid source when the backend supports none of the formats the
 * engine ships. */
inline ShaderSource selectShader(SDL_GPUDevice *device, ShaderProgram program, bool fragment) {
  ShaderSource source;
  const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
  ShaderBlobs blobs;
  if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0) {
    source.format = SDL_GPU_SHADERFORMAT_MSL;
    blobs = mslBlobs(program);
  } else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0) {
    source.format = SDL_GPU_SHADERFORMAT_SPIRV;
    blobs = spirvBlobs(program);
  } else if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0) {
    source.format = SDL_GPU_SHADERFORMAT_DXIL;
    blobs = dxilBlobs(program);
  } else {
    return source;
  }
  source.code = fragment ? blobs.fragment : blobs.vertex;
  source.size = fragment ? blobs.fragment_size : blobs.vertex_size;
  source.entry = fragment ? blobs.fragment_entry : blobs.vertex_entry;
  return source;
}

}  // namespace renderer_detail
}  // namespace xtgui

#endif /* XT_GUI_RENDERER_SHADERS_H */
