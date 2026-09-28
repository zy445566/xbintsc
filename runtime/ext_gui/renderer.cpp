/*
 * xbintsc GUI engine — SDL_GPU 2D renderer: shared state, shape pipeline and
 * the public geometry API.
 *
 * Text (glyph atlas) lives in `renderer_text.cpp` and image textures in
 * `renderer_image.cpp`; both are declared in `renderer_internal.h`.
 */

#include "renderer_internal.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "paint.h"
#include "text.h"

#include "renderer_shaders.h"

namespace xtgui {
namespace renderer_detail {

SDL_GPUGraphicsPipeline *g_pipeline = nullptr;
SDL_GPUGraphicsPipeline *g_text_pipeline = nullptr;
SDL_GPUGraphicsPipeline *g_image_pipeline = nullptr;
SDL_GPUSampler *g_sampler = nullptr;
int g_renderer_failed = 0;
bool g_text_ok = false;
std::vector<ImageBatch> g_image_batches;
std::unordered_map<std::string, SDL_GPUTexture *> g_image_textures;
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

}  // namespace renderer_detail
}  // namespace xtgui

bool xt_gui_renderer_ensure(SDL_GPUDevice *device, SDL_Window *reference) {
  using namespace xtgui::renderer_detail;
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
  g_image_pipeline = createImagePipeline(device, reference);
  g_sampler = createSampler(device);
  g_text_ok = g_text_pipeline != nullptr && g_sampler != nullptr;
  return true;
}

void xt_gui_renderer_destroy(SDL_GPUDevice *device) {
  using namespace xtgui::renderer_detail;
  if (g_pipeline != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline(device, g_pipeline);
    g_pipeline = nullptr;
  }
  if (g_text_pipeline != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline(device, g_text_pipeline);
    g_text_pipeline = nullptr;
  }
  if (g_image_pipeline != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline(device, g_image_pipeline);
    g_image_pipeline = nullptr;
  }
  if (g_sampler != nullptr) {
    SDL_ReleaseGPUSampler(device, g_sampler);
    g_sampler = nullptr;
  }
  for (auto &entry : g_image_textures) {
    if (entry.second != nullptr) SDL_ReleaseGPUTexture(device, entry.second);
  }
  g_image_textures.clear();
  g_image_batches.clear();
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
  using namespace xtgui::renderer_detail;
  std::vector<Vertex> verts;
  verts.reserve(list.rects.size() * 6);
  for (const xtgui::PaintRect &rect : list.rects) pushRect(verts, rect);
  uploadVertices(device, geometry, verts.data(), (uint32_t)verts.size(), sizeof(Vertex));
}

void xt_gui_renderer_draw(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                          XtGuiGeometry *geometry, float viewport_width, float viewport_height) {
  using namespace xtgui::renderer_detail;
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
