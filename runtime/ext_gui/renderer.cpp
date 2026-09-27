/*
 * xbintsc GUI engine — SDL_GPU 2D renderer implementation.
 */

#include "renderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <vector>

#include "paint.h"
#include "text.h"

namespace {

/* Rounded-rectangle fill. Vertex attributes (matching SDL_GPUVertexAttribute):
 *   0 -> position (pixels, viewport-relative)
 *   1 -> local   (pixels, relative to the rectangle centre)
 *   2 -> half    (rectangle half extents, pixels)
 *   3 -> radius  (pixels)
 *   4 -> colour
 * The push constant is the viewport size in logical pixels. */
const char *const kFillMSL = R"MSL(
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
const char *const kTextMSL = R"MSL(
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
)MSL";

struct Vertex {
  float x, y;
  float lx, ly;
  float hx, hy;
  float radius;
  float r, g, b, a;
};

struct TextVertex {
  float x, y;
  float u, v;
  float r, g, b, a;
};

SDL_GPUGraphicsPipeline *g_pipeline = nullptr;
SDL_GPUGraphicsPipeline *g_text_pipeline = nullptr;
SDL_GPUSampler *g_sampler = nullptr;
int g_renderer_failed = 0;
bool g_text_ok = false;

/* -- glyph atlas ---------------------------------------------------------- */

constexpr int kAtlasSize = 2048;

struct GlyphEntry {
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  int width = 0, height = 0, left = 0, top = 0;
};

struct GlyphKey {
  const void *font = nullptr;
  uint32_t glyph = 0;
  bool operator==(const GlyphKey &other) const {
    return font == other.font && glyph == other.glyph;
  }
};

struct GlyphKeyHash {
  size_t operator()(const GlyphKey &key) const {
    return std::hash<const void *>()(key.font) * 1099511628211ull ^
           (size_t)(key.glyph * 2654435761u);
  }
};

SDL_GPUTexture *g_atlas_texture = nullptr;
std::vector<unsigned char> g_atlas_mirror;
std::unordered_map<GlyphKey, GlyphEntry, GlyphKeyHash> g_glyphs;
int g_atlas_x = 0;
int g_atlas_y = 0;
int g_atlas_row_height = 0;
bool g_atlas_dirty = false;
int g_atlas_dirty_x0 = 0, g_atlas_dirty_y0 = 0, g_atlas_dirty_x1 = 0, g_atlas_dirty_y1 = 0;
bool g_atlas_full_logged = false;

void pushRect(std::vector<Vertex> &verts, const xtgui::PaintRect &rect) {
  float hw = rect.width * 0.5f;
  float hh = rect.height * 0.5f;
  float cx = rect.x + hw;
  float cy = rect.y + hh;
  float radius = std::min(rect.radius, std::min(hw, hh));

  const float local_x[4] = {-hw, hw, hw, -hw};
  const float local_y[4] = {-hh, -hh, hh, hh};
  Vertex corners[4];
  for (int i = 0; i < 4; i++) {
    corners[i].x = cx + local_x[i];
    corners[i].y = cy + local_y[i];
    corners[i].lx = local_x[i];
    corners[i].ly = local_y[i];
    corners[i].hx = hw;
    corners[i].hy = hh;
    corners[i].radius = radius;
    corners[i].r = rect.color.r;
    corners[i].g = rect.color.g;
    corners[i].b = rect.color.b;
    corners[i].a = rect.color.a;
  }
  static const int order[6] = {0, 1, 2, 0, 2, 3};
  for (int i = 0; i < 6; i++) verts.push_back(corners[order[i]]);
}

void pushGlyph(std::vector<TextVertex> &verts, float x, float y, float width, float height,
               const GlyphEntry &entry, const xtgui::Color &color) {
  const float corners_x[4] = {x, x + width, x + width, x};
  const float corners_y[4] = {y, y, y + height, y + height};
  const float u[4] = {entry.u0, entry.u1, entry.u1, entry.u0};
  const float v[4] = {entry.v0, entry.v0, entry.v1, entry.v1};
  TextVertex corners[4];
  for (int i = 0; i < 4; i++) {
    corners[i].x = corners_x[i];
    corners[i].y = corners_y[i];
    corners[i].u = u[i];
    corners[i].v = v[i];
    corners[i].r = color.r;
    corners[i].g = color.g;
    corners[i].b = color.b;
    corners[i].a = color.a;
  }
  static const int order[6] = {0, 1, 2, 0, 2, 3};
  for (int i = 0; i < 6; i++) verts.push_back(corners[order[i]]);
}

bool ensureAtlasTexture(SDL_GPUDevice *device) {
  if (g_atlas_texture != nullptr) return true;
  SDL_GPUTextureCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = kAtlasSize;
  info.height = kAtlasSize;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  g_atlas_texture = SDL_CreateGPUTexture(device, &info);
  if (g_atlas_texture == nullptr) {
    fprintf(stderr, "xt_gui: glyph atlas creation failed: %s\n", SDL_GetError());
    return false;
  }
  g_atlas_mirror.assign((size_t)kAtlasSize * kAtlasSize, 0);
  return true;
}

void markAtlasDirty(int x, int y, int width, int height) {
  if (!g_atlas_dirty) {
    g_atlas_dirty_x0 = x;
    g_atlas_dirty_y0 = y;
    g_atlas_dirty_x1 = x + width;
    g_atlas_dirty_y1 = y + height;
    g_atlas_dirty = true;
    return;
  }
  g_atlas_dirty_x0 = std::min(g_atlas_dirty_x0, x);
  g_atlas_dirty_y0 = std::min(g_atlas_dirty_y0, y);
  g_atlas_dirty_x1 = std::max(g_atlas_dirty_x1, x + width);
  g_atlas_dirty_y1 = std::max(g_atlas_dirty_y1, y + height);
}

/** Rasterise + pack `glyph` if new. Returns true and fills `out` when the glyph
 * has a bitmap; false for blank glyphs (space, missing) or a full atlas. */
bool ensureGlyph(SDL_GPUDevice *device, xtgui::Font *font, uint32_t glyph, GlyphEntry &out) {
  GlyphKey key{font, glyph};
  auto found = g_glyphs.find(key);
  if (found != g_glyphs.end()) {
    out = found->second;
    return out.width > 0 && out.height > 0;
  }

  xtgui::GlyphImage image;
  if (!xtgui::xt_text_rasterize(font, glyph, image) || image.width <= 0 || image.height <= 0) {
    g_glyphs.emplace(key, GlyphEntry{});
    return false;
  }
  if (!ensureAtlasTexture(device)) return false;

  if (g_atlas_x + image.width > kAtlasSize) {
    g_atlas_x = 0;
    g_atlas_y += g_atlas_row_height + 1;
    g_atlas_row_height = 0;
  }
  if (g_atlas_y + image.height > kAtlasSize) {
    if (!g_atlas_full_logged) {
      fprintf(stderr, "xt_gui: glyph atlas full; some glyphs will not be drawn\n");
      g_atlas_full_logged = true;
    }
    g_glyphs.emplace(key, GlyphEntry{});
    return false;
  }

  int px = g_atlas_x;
  int py = g_atlas_y;
  for (int row = 0; row < image.height; row++) {
    std::memcpy(&g_atlas_mirror[(size_t)(py + row) * kAtlasSize + px],
                image.pixels.data() + (size_t)row * image.width, (size_t)image.width);
  }
  markAtlasDirty(px, py, image.width, image.height);

  GlyphEntry entry;
  entry.u0 = (float)px / kAtlasSize;
  entry.v0 = (float)py / kAtlasSize;
  entry.u1 = (float)(px + image.width) / kAtlasSize;
  entry.v1 = (float)(py + image.height) / kAtlasSize;
  entry.width = image.width;
  entry.height = image.height;
  entry.left = image.left;
  entry.top = image.top;
  g_glyphs.emplace(key, entry);

  g_atlas_x += image.width + 1;
  g_atlas_row_height = std::max(g_atlas_row_height, image.height);
  out = entry;
  return true;
}

void flushAtlas(SDL_GPUDevice *device) {
  if (!g_atlas_dirty || g_atlas_texture == nullptr) return;
  g_atlas_dirty = false;
  int x0 = g_atlas_dirty_x0;
  int y0 = g_atlas_dirty_y0;
  int width = g_atlas_dirty_x1 - g_atlas_dirty_x0;
  int height = g_atlas_dirty_y1 - g_atlas_dirty_y0;
  if (width <= 0 || height <= 0) return;

  uint32_t bytes = (uint32_t)(width * height);
  SDL_GPUTransferBufferCreateInfo transfer_info;
  memset(&transfer_info, 0, sizeof(transfer_info));
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_info.size = bytes;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
  if (transfer == nullptr) return;
  unsigned char *mapped = (unsigned char *)SDL_MapGPUTransferBuffer(device, transfer, false);
  if (mapped == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return;
  }
  for (int row = 0; row < height; row++) {
    std::memcpy(mapped + (size_t)row * width,
                &g_atlas_mirror[(size_t)(y0 + row) * kAtlasSize + x0], (size_t)width);
  }
  SDL_UnmapGPUTransferBuffer(device, transfer);

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  if (cmd == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return;
  }
  SDL_GPUTextureTransferInfo source;
  memset(&source, 0, sizeof(source));
  source.transfer_buffer = transfer;
  source.offset = 0;
  source.pixels_per_row = (Uint32)width;
  source.rows_per_layer = (Uint32)height;
  SDL_GPUTextureRegion destination;
  memset(&destination, 0, sizeof(destination));
  destination.texture = g_atlas_texture;
  destination.mip_level = 0;
  destination.layer = 0;
  destination.x = (Uint32)x0;
  destination.y = (Uint32)y0;
  destination.z = 0;
  destination.w = (Uint32)width;
  destination.h = (Uint32)height;
  destination.d = 1;
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_UploadToGPUTexture(copy, &source, &destination, false);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
}

/** Upload `count` vertices (of `stride` bytes) into `geometry`, reusing or
 * growing its GPU buffer. Does nothing when `count` is zero. */
void uploadVertices(SDL_GPUDevice *device, XtGuiGeometry *geometry, const void *data,
                    uint32_t count, uint32_t stride) {
  geometry->vertex_count = count;
  if (count == 0) return;
  uint32_t bytes = count * stride;
  if (geometry->buffer == nullptr || geometry->capacity_bytes < bytes) {
    if (geometry->buffer != nullptr) SDL_ReleaseGPUBuffer(device, geometry->buffer);
    SDL_GPUBufferCreateInfo buffer_info;
    memset(&buffer_info, 0, sizeof(buffer_info));
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer_info.size = bytes;
    geometry->buffer = SDL_CreateGPUBuffer(device, &buffer_info);
    geometry->capacity_bytes = bytes;
  }
  if (geometry->buffer == nullptr) {
    fprintf(stderr, "xt_gui: vertex buffer creation failed: %s\n", SDL_GetError());
    geometry->vertex_count = 0;
    return;
  }

  SDL_GPUTransferBufferCreateInfo transfer_info;
  memset(&transfer_info, 0, sizeof(transfer_info));
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_info.size = bytes;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
  if (transfer == nullptr) {
    fprintf(stderr, "xt_gui: transfer buffer creation failed: %s\n", SDL_GetError());
    geometry->vertex_count = 0;
    return;
  }
  void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  std::memcpy(mapped, data, bytes);
  SDL_UnmapGPUTransferBuffer(device, transfer);

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  if (cmd == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    geometry->vertex_count = 0;
    return;
  }
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTransferBufferLocation source;
  source.transfer_buffer = transfer;
  source.offset = 0;
  SDL_GPUBufferRegion destination;
  destination.buffer = geometry->buffer;
  destination.offset = 0;
  destination.size = bytes;
  SDL_UploadToGPUBuffer(copy, &source, &destination, false);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
}

SDL_GPUGraphicsPipeline *createTextPipeline(SDL_GPUDevice *device, SDL_Window *reference) {
  SDL_GPUShaderCreateInfo vs_info;
  memset(&vs_info, 0, sizeof(vs_info));
  vs_info.code_size = strlen(kTextMSL);
  vs_info.code = (const Uint8 *)kTextMSL;
  vs_info.entrypoint = "vs_text";
  vs_info.format = SDL_GPU_SHADERFORMAT_MSL;
  vs_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
  vs_info.num_uniform_buffers = 1;
  SDL_GPUShader *vs = SDL_CreateGPUShader(device, &vs_info);

  SDL_GPUShaderCreateInfo fs_info;
  memset(&fs_info, 0, sizeof(fs_info));
  fs_info.code_size = strlen(kTextMSL);
  fs_info.code = (const Uint8 *)kTextMSL;
  fs_info.entrypoint = "fs_text";
  fs_info.format = SDL_GPU_SHADERFORMAT_MSL;
  fs_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  fs_info.num_samplers = 1;
  SDL_GPUShader *fs = SDL_CreateGPUShader(device, &fs_info);

  if (vs == nullptr || fs == nullptr) {
    fprintf(stderr, "xt_gui: text shader creation failed: %s\n", SDL_GetError());
    if (vs != nullptr) SDL_ReleaseGPUShader(device, vs);
    if (fs != nullptr) SDL_ReleaseGPUShader(device, fs);
    return nullptr;
  }

  SDL_GPUVertexBufferDescription vb_desc;
  memset(&vb_desc, 0, sizeof(vb_desc));
  vb_desc.slot = 0;
  vb_desc.pitch = sizeof(TextVertex);
  vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

  SDL_GPUVertexAttribute attrs[3];
  memset(attrs, 0, sizeof(attrs));
  attrs[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (Uint32)offsetof(TextVertex, x)};
  attrs[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (Uint32)offsetof(TextVertex, u)};
  attrs[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (Uint32)offsetof(TextVertex, r)};

  SDL_GPUColorTargetDescription color_target;
  memset(&color_target, 0, sizeof(color_target));
  color_target.format = SDL_GetGPUSwapchainTextureFormat(device, reference);
  color_target.blend_state.enable_blend = true;
  color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

  SDL_GPUGraphicsPipelineCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.vertex_shader = vs;
  info.fragment_shader = fs;
  info.vertex_input_state.vertex_buffer_descriptions = &vb_desc;
  info.vertex_input_state.num_vertex_buffers = 1;
  info.vertex_input_state.vertex_attributes = attrs;
  info.vertex_input_state.num_vertex_attributes = 3;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  info.target_info.color_target_descriptions = &color_target;
  info.target_info.num_color_targets = 1;

  SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
  SDL_ReleaseGPUShader(device, vs);
  SDL_ReleaseGPUShader(device, fs);
  if (pipeline == nullptr) {
    fprintf(stderr, "xt_gui: text pipeline creation failed: %s\n", SDL_GetError());
  }
  return pipeline;
}

SDL_GPUSampler *createSampler(SDL_GPUDevice *device) {
  SDL_GPUSamplerCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.min_filter = SDL_GPU_FILTER_LINEAR;
  info.mag_filter = SDL_GPU_FILTER_LINEAR;
  info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &info);
  if (sampler == nullptr) {
    fprintf(stderr, "xt_gui: glyph sampler creation failed: %s\n", SDL_GetError());
  }
  return sampler;
}

}  // namespace

bool xt_gui_renderer_ensure(SDL_GPUDevice *device, SDL_Window *reference) {
  if (g_pipeline != nullptr) return true;
  if (g_renderer_failed) return false;
  if ((SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_MSL) == 0) {
    fprintf(stderr,
            "xt_gui: backend has no MSL shader support; shape/text painting disabled "
            "(Vulkan/D3D12 shader blobs are a TODO)\n");
    g_renderer_failed = 1;
    return false;
  }

  SDL_GPUShaderCreateInfo vs_info;
  memset(&vs_info, 0, sizeof(vs_info));
  vs_info.code_size = strlen(kFillMSL);
  vs_info.code = (const Uint8 *)kFillMSL;
  vs_info.entrypoint = "vs_main";
  vs_info.format = SDL_GPU_SHADERFORMAT_MSL;
  vs_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
  vs_info.num_uniform_buffers = 1;
  SDL_GPUShader *vs = SDL_CreateGPUShader(device, &vs_info);

  SDL_GPUShaderCreateInfo fs_info;
  memset(&fs_info, 0, sizeof(fs_info));
  fs_info.code_size = strlen(kFillMSL);
  fs_info.code = (const Uint8 *)kFillMSL;
  fs_info.entrypoint = "fs_main";
  fs_info.format = SDL_GPU_SHADERFORMAT_MSL;
  fs_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  SDL_GPUShader *fs = SDL_CreateGPUShader(device, &fs_info);

  if (vs == nullptr || fs == nullptr) {
    fprintf(stderr, "xt_gui: shader creation failed: %s\n", SDL_GetError());
    if (vs != nullptr) SDL_ReleaseGPUShader(device, vs);
    if (fs != nullptr) SDL_ReleaseGPUShader(device, fs);
    g_renderer_failed = 1;
    return false;
  }

  SDL_GPUVertexBufferDescription vb_desc;
  memset(&vb_desc, 0, sizeof(vb_desc));
  vb_desc.slot = 0;
  vb_desc.pitch = sizeof(Vertex);
  vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

  SDL_GPUVertexAttribute attrs[5];
  memset(attrs, 0, sizeof(attrs));
  attrs[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (Uint32)offsetof(Vertex, x)};
  attrs[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (Uint32)offsetof(Vertex, lx)};
  attrs[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (Uint32)offsetof(Vertex, hx)};
  attrs[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, (Uint32)offsetof(Vertex, radius)};
  attrs[4] = {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (Uint32)offsetof(Vertex, r)};

  SDL_GPUColorTargetDescription color_target;
  memset(&color_target, 0, sizeof(color_target));
  color_target.format = SDL_GetGPUSwapchainTextureFormat(device, reference);
  color_target.blend_state.enable_blend = true;
  color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

  SDL_GPUGraphicsPipelineCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.vertex_shader = vs;
  info.fragment_shader = fs;
  info.vertex_input_state.vertex_buffer_descriptions = &vb_desc;
  info.vertex_input_state.num_vertex_buffers = 1;
  info.vertex_input_state.vertex_attributes = attrs;
  info.vertex_input_state.num_vertex_attributes = 5;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  info.target_info.color_target_descriptions = &color_target;
  info.target_info.num_color_targets = 1;

  g_pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
  SDL_ReleaseGPUShader(device, vs);
  SDL_ReleaseGPUShader(device, fs);
  if (g_pipeline == nullptr) {
    fprintf(stderr, "xt_gui: pipeline creation failed: %s\n", SDL_GetError());
    g_renderer_failed = 1;
    return false;
  }

  g_text_pipeline = createTextPipeline(device, reference);
  g_sampler = createSampler(device);
  g_text_ok = g_text_pipeline != nullptr && g_sampler != nullptr;
  return true;
}

void xt_gui_renderer_destroy(SDL_GPUDevice *device) {
  if (g_pipeline != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline(device, g_pipeline);
    g_pipeline = nullptr;
  }
  if (g_text_pipeline != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline(device, g_text_pipeline);
    g_text_pipeline = nullptr;
  }
  if (g_sampler != nullptr) {
    SDL_ReleaseGPUSampler(device, g_sampler);
    g_sampler = nullptr;
  }
  if (g_atlas_texture != nullptr) {
    SDL_ReleaseGPUTexture(device, g_atlas_texture);
    g_atlas_texture = nullptr;
  }
  g_atlas_mirror.clear();
  g_glyphs.clear();
  g_atlas_x = 0;
  g_atlas_y = 0;
  g_atlas_row_height = 0;
  g_atlas_dirty = false;
  g_atlas_full_logged = false;
  g_text_ok = false;
  g_renderer_failed = 0;
}

void xt_gui_geometry_destroy(SDL_GPUDevice *device, XtGuiGeometry *geometry) {
  if (geometry->buffer != nullptr) {
    SDL_ReleaseGPUBuffer(device, geometry->buffer);
    geometry->buffer = nullptr;
  }
  geometry->capacity_bytes = 0;
  geometry->vertex_count = 0;
  geometry->dirty = true;
}

void xt_gui_geometry_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                            const xtgui::DisplayList &list) {
  std::vector<Vertex> verts;
  verts.reserve(list.rects.size() * 6);
  for (const xtgui::PaintRect &rect : list.rects) pushRect(verts, rect);
  uploadVertices(device, geometry, verts.data(), (uint32_t)verts.size(), sizeof(Vertex));
}

void xt_gui_text_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                        const xtgui::DisplayList &list, float scale) {
  if (!g_text_ok) {
    geometry->vertex_count = 0;
    return;
  }
  if (scale <= 0.0f) scale = 1.0f;

  std::vector<TextVertex> verts;
  for (const xtgui::PaintText &run : list.texts) {
    if (run.text.empty() || run.color.a <= 0.0f) continue;
    xtgui::Font *logical = xtgui::xt_text_resolve(run.font);
    if (logical == nullptr) continue;
    xtgui::FontSpec raster_spec = run.font;
    raster_spec.pixel_size = run.font.pixel_size * scale;
    xtgui::Font *raster = xtgui::xt_text_resolve(raster_spec);
    if (raster == nullptr) raster = logical;

    std::vector<xtgui::ShapedGlyph> glyphs;
    xtgui::xt_text_shape_run(run.text, logical, glyphs);
    float pen_x = run.x;
    for (const xtgui::ShapedGlyph &glyph : glyphs) {
      if (glyph.glyph != 0) {
        GlyphEntry entry;
        if (ensureGlyph(device, raster, glyph.glyph, entry)) {
          float quad_x = pen_x + glyph.x_offset + (float)entry.left / scale;
          float quad_y = run.baseline + glyph.y_offset - (float)entry.top / scale;
          float quad_w = (float)entry.width / scale;
          float quad_h = (float)entry.height / scale;
          pushGlyph(verts, quad_x, quad_y, quad_w, quad_h, entry, run.color);
        }
      }
      pen_x += glyph.x_advance;
    }
  }
  uploadVertices(device, geometry, verts.data(), (uint32_t)verts.size(), sizeof(TextVertex));
  flushAtlas(device);
}

void xt_gui_renderer_draw(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                          XtGuiGeometry *geometry, float viewport_width, float viewport_height) {
  if (g_pipeline == nullptr || geometry->buffer == nullptr || geometry->vertex_count == 0) return;
  if (viewport_width <= 0.0f || viewport_height <= 0.0f) return;
  SDL_BindGPUGraphicsPipeline(pass, g_pipeline);
  SDL_GPUBufferBinding binding;
  binding.buffer = geometry->buffer;
  binding.offset = 0;
  SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
  float viewport[2] = {viewport_width, viewport_height};
  SDL_PushGPUVertexUniformData(cmd, 0, viewport, sizeof(viewport));
  SDL_DrawGPUPrimitives(pass, geometry->vertex_count, 1, 0, 0);
}

void xt_gui_text_draw(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass, XtGuiGeometry *geometry,
                      float viewport_width, float viewport_height) {
  if (!g_text_ok || geometry->buffer == nullptr || geometry->vertex_count == 0) return;
  if (g_atlas_texture == nullptr) return;
  if (viewport_width <= 0.0f || viewport_height <= 0.0f) return;
  SDL_BindGPUGraphicsPipeline(pass, g_text_pipeline);
  SDL_GPUBufferBinding binding;
  binding.buffer = geometry->buffer;
  binding.offset = 0;
  SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
  SDL_GPUTextureSamplerBinding atlas_binding;
  atlas_binding.texture = g_atlas_texture;
  atlas_binding.sampler = g_sampler;
  SDL_BindGPUFragmentSamplers(pass, 0, &atlas_binding, 1);
  float viewport[2] = {viewport_width, viewport_height};
  SDL_PushGPUVertexUniformData(cmd, 0, viewport, sizeof(viewport));
  SDL_DrawGPUPrimitives(pass, geometry->vertex_count, 1, 0, 0);
}
