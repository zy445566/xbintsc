/*
 * xbintsc GUI engine — SDL_GPU 2D renderer: glyph atlas and text geometry.
 */

#include "renderer_internal.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "text.h"

#include "perf.h"

#include "renderer_shaders.h"

namespace xtgui {
namespace renderer_detail {

/** Bounds for the atlas raster of a transformed glyph: below 1 the text is
 * already shrinking, above 4 the atlas cost outweighs the sharpness. */
constexpr float kMinGlyphScale = 0.5f;
constexpr float kMaxGlyphScale = 4.0f;

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

void flushAtlas(SDL_GPUDevice *device, XtGuiUploadBatch *batch) {
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

  SDL_GPUCommandBuffer *cmd = uploadCommand(device, batch);
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
  uploadDone(cmd, batch);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
}

SDL_GPUGraphicsPipeline *createTextPipeline(SDL_GPUDevice *device, SDL_Window *reference) {
  const ShaderSource text_vs = selectShader(device, ShaderProgram::Text, false);
  const ShaderSource text_fs = selectShader(device, ShaderProgram::Text, true);
  if (!text_vs.valid() || !text_fs.valid()) return nullptr;

  SDL_GPUShaderCreateInfo vs_info;
  memset(&vs_info, 0, sizeof(vs_info));
  vs_info.code_size = text_vs.size;
  vs_info.code = text_vs.code;
  vs_info.entrypoint = text_vs.entry;
  vs_info.format = text_vs.format;
  vs_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
  vs_info.num_uniform_buffers = 1;
  SDL_GPUShader *vs = SDL_CreateGPUShader(device, &vs_info);

  SDL_GPUShaderCreateInfo fs_info;
  memset(&fs_info, 0, sizeof(fs_info));
  fs_info.code_size = text_fs.size;
  fs_info.code = text_fs.code;
  fs_info.entrypoint = text_fs.entry;
  fs_info.format = text_fs.format;
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

}  // namespace renderer_detail
}  // namespace xtgui

void xt_gui_text_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                        const xtgui::DisplayList &list, float scale, XtGuiUploadBatch *batch) {
  using namespace xtgui::renderer_detail;
  if (!g_text_ok) {
    geometry->vertex_count = 0;
    return;
  }
  if (scale <= 0.0f) scale = 1.0f;

  std::vector<TextVertex> &verts = g_text_staging;
  verts.clear();
  geometry->op_counts.assign(list.texts.size(), 0);
  for (const xtgui::PaintOp &op : list.ops) {
    if (op.kind != xtgui::PaintKind::Text || op.index >= list.texts.size()) continue;
    const xtgui::PaintText &run = list.texts[op.index];
    if (run.text.empty() || run.color.a <= 0.0f) continue;
    xtgui::Font *logical = xtgui::xt_text_resolve(run.font);
    if (logical == nullptr) continue;

    /* A scaling `transform` magnifies the glyph quads on the GPU, so the atlas
       has to hold a raster of that size or the text looks like a scaled bitmap:
       rasterise at the device scale times the transform's magnification, and
       clamp it — the atlas is finite, and a 40x transform would evict every
       other glyph (a magnified bitmap is the right trade at that point). */
    float glyph_scale = scale * run.transform.magnification();
    if (glyph_scale < kMinGlyphScale) glyph_scale = kMinGlyphScale;
    if (glyph_scale > kMaxGlyphScale) glyph_scale = kMaxGlyphScale;

    const xtgui::ShapedRun *shaped =
        xtgui::xt_text_shape_cached(run.text, logical, run.font.letter_spacing);
    if (shaped == nullptr) continue;
    float pen_x = run.x;
    uint32_t before = (uint32_t)verts.size();
    /* The scaled face is a per-run decision (the size and matrix are constant
     * for the run); resolving it per glyph used to build a 512-byte key and
     * hash it for every character on a HiDPI display. */
    xtgui::Font *scaled_input = nullptr;
    xtgui::Font *scaled_output = nullptr;
    for (const xtgui::ShapedGlyph &glyph : shaped->glyphs) {
      if (glyph.glyph != 0) {
        xtgui::Font *face = glyph.font != nullptr ? glyph.font : logical;
        if (glyph_scale != 1.0f) {
          if (face != scaled_input) {
            xtgui::Font *scaled = xtgui::xt_text_scale(face, run.font.pixel_size * glyph_scale);
            scaled_input = face;
            scaled_output = scaled != nullptr ? scaled : face;
          }
          face = scaled_output;
        }
        GlyphEntry entry;
        if (ensureGlyph(device, face, glyph.glyph, entry)) {
          /* Entry metrics are in raster pixels; divide them back into logical
             units — the transform (not the atlas) does the final scaling. */
          float quad_x = pen_x + glyph.x_offset + (float)entry.left / glyph_scale;
          float quad_y = run.baseline + glyph.y_offset - (float)entry.top / glyph_scale;
          float quad_w = (float)entry.width / glyph_scale;
          float quad_h = (float)entry.height / glyph_scale;
          pushGlyph(verts, quad_x, quad_y, quad_w, quad_h, entry, run.color, run.transform,
                    op.clip, list);
        }
      }
      pen_x += glyph.x_advance;
    }
    geometry->op_counts[op.index] = (uint32_t)verts.size() - before;
  }
  uploadVertices(device, geometry, verts.data(), (uint32_t)verts.size(), sizeof(TextVertex), batch);
  flushAtlas(device, batch);
}
