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
#include "perf.h"
#include "text.h"

#include "renderer_shaders.h"

namespace xtgui {
namespace renderer_detail {

SDL_GPUGraphicsPipeline *g_pipeline = nullptr;
SDL_GPUGraphicsPipeline *g_text_pipeline = nullptr;
SDL_GPUGraphicsPipeline *g_image_pipeline = nullptr;
SDL_GPUSampler *g_sampler = nullptr;
SDL_GPUDevice *g_device = nullptr;
SDL_GPUTextureFormat g_swapchain_format = SDL_GPU_TEXTUREFORMAT_INVALID;
int g_renderer_failed = 0;
bool g_text_ok = false;
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

std::vector<Vertex> g_shape_staging;
std::vector<TextVertex> g_text_staging;
std::vector<TextVertex> g_image_staging;

/** The command buffer an upload records into. With a batch, the frame's single
 * command buffer is acquired lazily and submitted by `xt_gui_upload_end`;
 * without one, each upload submits on its own. */
SDL_GPUCommandBuffer *uploadCommand(SDL_GPUDevice *device, XtGuiUploadBatch *batch) {
  if (batch == nullptr) return SDL_AcquireGPUCommandBuffer(device);
  if (batch->cmd == nullptr) batch->cmd = SDL_AcquireGPUCommandBuffer(batch->device);
  return batch->cmd;
}

void uploadDone(SDL_GPUCommandBuffer *cmd, XtGuiUploadBatch *batch) {
  if (batch != nullptr) {
    batch->recorded += 1;
    return;
  }
  SDL_SubmitGPUCommandBuffer(cmd);
  g_perf_frame.uploadSubmits += 1;
}

/** Upload `count` vertices (of `stride` bytes) into `geometry`, reusing or
 * growing its GPU buffer. Does nothing when `count` is zero. */
void uploadVertices(SDL_GPUDevice *device, XtGuiGeometry *geometry, const void *data,
                    uint32_t count, uint32_t stride, XtGuiUploadBatch *batch) {
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

  /* Reuse the geometry's staging buffer: a repaint used to create and release a
   * transfer buffer for every vertex array, every frame. */
  if (geometry->staging == nullptr || geometry->staging_capacity < bytes) {
    if (geometry->staging != nullptr) SDL_ReleaseGPUTransferBuffer(device, geometry->staging);
    SDL_GPUTransferBufferCreateInfo transfer_info;
    memset(&transfer_info, 0, sizeof(transfer_info));
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = bytes;
    geometry->staging = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    geometry->staging_capacity = geometry->staging != nullptr ? bytes : 0;
  }
  if (geometry->staging == nullptr) {
    fprintf(stderr, "xt_gui: transfer buffer creation failed: %s\n", SDL_GetError());
    geometry->vertex_count = 0;
    return;
  }
  /* `cycle` rotates SDL's internal allocation while the previous frame's copy
   * may still be in flight: the staging buffer is reused every frame, so the
   * non-cycling map could overwrite data the GPU has not read yet. */
  void *mapped = SDL_MapGPUTransferBuffer(device, geometry->staging, true);
  std::memcpy(mapped, data, bytes);
  SDL_UnmapGPUTransferBuffer(device, geometry->staging);

  SDL_GPUCommandBuffer *cmd = uploadCommand(device, batch);
  if (cmd == nullptr) {
    geometry->vertex_count = 0;
    return;
  }
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTransferBufferLocation source;
  source.transfer_buffer = geometry->staging;
  source.offset = 0;
  SDL_GPUBufferRegion destination;
  destination.buffer = geometry->buffer;
  destination.offset = 0;
  destination.size = bytes;
  SDL_UploadToGPUBuffer(copy, &source, &destination, false);
  SDL_EndGPUCopyPass(copy);
  uploadDone(cmd, batch);
  g_perf_frame.vertices += count;
}

}  // namespace renderer_detail
}  // namespace xtgui

void xt_gui_upload_begin(XtGuiUploadBatch *batch, SDL_GPUDevice *device) {
  batch->device = device;
  batch->cmd = nullptr;
  batch->recorded = 0;
}

void xt_gui_upload_end(XtGuiUploadBatch *batch) {
  if (batch == nullptr || batch->cmd == nullptr) return;
  SDL_SubmitGPUCommandBuffer(batch->cmd);
  batch->cmd = nullptr;
  xtgui::g_perf_frame.uploadSubmits += 1;
}

bool xt_gui_renderer_ensure(SDL_GPUDevice *device, SDL_Window *reference) {
  using namespace xtgui::renderer_detail;
  if (g_pipeline != nullptr) return true;
  if (g_renderer_failed) return false;
  g_device = device;
  g_swapchain_format = SDL_GetGPUSwapchainTextureFormat(device, reference);
  const ShaderSource fill_vs = selectShader(device, ShaderProgram::Fill, false);
  const ShaderSource fill_fs = selectShader(device, ShaderProgram::Fill, true);
  if (!fill_vs.valid() || !fill_fs.valid()) {
    fprintf(stderr,
            "xt_gui: backend supports neither Metal nor D3D12 shaders; shape/text painting "
            "disabled (Vulkan SPIR-V blobs are a TODO)\n");
    g_renderer_failed = 1;
    return false;
  }

  SDL_GPUShaderCreateInfo vs_info;
  memset(&vs_info, 0, sizeof(vs_info));
  vs_info.code_size = fill_vs.size;
  vs_info.code = fill_vs.code;
  vs_info.entrypoint = fill_vs.entry;
  vs_info.format = fill_vs.format;
  vs_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
  vs_info.num_uniform_buffers = 1;
  SDL_GPUShader *vs = SDL_CreateGPUShader(device, &vs_info);

  SDL_GPUShaderCreateInfo fs_info;
  memset(&fs_info, 0, sizeof(fs_info));
  fs_info.code_size = fill_fs.size;
  fs_info.code = fill_fs.code;
  fs_info.entrypoint = fill_fs.entry;
  fs_info.format = fill_fs.format;
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
  xtgui::renderer_detail::layerRendererDestroy(device);
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
  g_device = nullptr;
  g_swapchain_format = SDL_GPU_TEXTUREFORMAT_INVALID;
}

void xt_gui_geometry_destroy(SDL_GPUDevice *device, XtGuiGeometry *geometry) {
  if (geometry->buffer != nullptr) {
    SDL_ReleaseGPUBuffer(device, geometry->buffer);
    geometry->buffer = nullptr;
  }
  if (geometry->staging != nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, geometry->staging);
    geometry->staging = nullptr;
  }
  for (SDL_GPUTexture *texture : geometry->scene_textures) {
    if (texture != nullptr) SDL_ReleaseGPUTexture(device, texture);
  }
  geometry->scene_textures.clear();
  geometry->scene_width = 0;
  geometry->scene_height = 0;
  geometry->scene_format = SDL_GPU_TEXTUREFORMAT_INVALID;
  geometry->staging_capacity = 0;
  geometry->capacity_bytes = 0;
  geometry->vertex_count = 0;
  geometry->dirty = true;
}

void xt_gui_geometry_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                            const xtgui::DisplayList &list, XtGuiUploadBatch *batch) {
  using namespace xtgui::renderer_detail;
  std::vector<Vertex> &verts = g_shape_staging;
  verts.clear();
  verts.reserve(list.rects.size() * 6);
  geometry->op_counts.assign(list.rects.size(), 0);
  /* Walk painter order so each rectangle carries its op's clip path. */
  for (const xtgui::PaintOp &op : list.ops) {
    if (op.kind != xtgui::PaintKind::Rect || op.index >= list.rects.size()) continue;
    uint32_t before = (uint32_t)verts.size();
    pushShape(verts, list.rects[op.index], op.clip, list);
    geometry->op_counts[op.index] = (uint32_t)verts.size() - before;
  }
  uploadVertices(device, geometry, verts.data(), (uint32_t)verts.size(), sizeof(Vertex), batch);
}

void xt_gui_build_runs(XtGuiGeometry *shapes, XtGuiGeometry *texts, XtGuiGeometry *images,
                       const xtgui::DisplayList &list) {
  shapes->runs.clear();
  uint32_t cursor[3] = {0, 0, 0};
  for (const xtgui::PaintOp &op : list.ops) {
    if (op.kind == xtgui::PaintKind::LayerBegin ||
        op.kind == xtgui::PaintKind::LayerEnd) {
      XtGuiDrawRun marker;
      marker.pipeline = op.kind == xtgui::PaintKind::LayerBegin ? XtGuiDrawRun::LayerBegin
                                                                : XtGuiDrawRun::LayerEnd;
      marker.layer = op.kind == xtgui::PaintKind::LayerBegin ? op.index : 0;
      shapes->runs.push_back(marker);
      continue;
    }
    int pipeline = (int)op.kind;
    XtGuiGeometry *geometry = pipeline == 0 ? shapes : (pipeline == 1 ? texts : images);
    if (op.index >= geometry->op_counts.size()) continue;
    uint32_t count = geometry->op_counts[op.index];
    uint32_t first = cursor[pipeline];
    cursor[pipeline] += count;
    if (count == 0) continue;
    SDL_GPUTexture *texture = nullptr;
    if (pipeline == 2 && op.index < images->op_textures.size()) texture = images->op_textures[op.index];
    float cx = op.clip.active ? op.clip.x : 0.0f;
    float cy = op.clip.active ? op.clip.y : 0.0f;
    float cw = op.clip.active ? op.clip.width : 0.0f;
    float ch = op.clip.active ? op.clip.height : 0.0f;
    if (!shapes->runs.empty()) {
      XtGuiDrawRun &last = shapes->runs.back();
      if (last.pipeline == pipeline && last.first + last.count == first && last.clip_x == cx &&
          last.clip_y == cy && last.clip_width == cw && last.clip_height == ch &&
          last.texture == texture) {
        last.count += count;
        continue;
      }
    }
    XtGuiDrawRun run;
    run.pipeline = pipeline;
    run.first = first;
    run.count = count;
    run.clip_x = cx;
    run.clip_y = cy;
    run.clip_width = cw;
    run.clip_height = ch;
    run.texture = texture;
    shapes->runs.push_back(run);
  }
}

namespace xtgui {
namespace renderer_detail {

void drawRuns(SDL_GPURenderPass *pass, const std::vector<XtGuiDrawRun> &runs, size_t begin,
              size_t end, XtGuiGeometry *shapes, XtGuiGeometry *texts, XtGuiGeometry *images,
              int target_width, int target_height, float pixel_density) {
  if (pass == nullptr) return;
  /* A run list alternates pipelines (background, text, background, …), so the
   * same scissor rectangle and the same pipeline were re-bound for every run.
   * Track what is bound and only issue the state change when it actually
   * changes: a 1,000-row list has hundreds of runs. */
  SDL_Rect bound_scissor;
  bound_scissor.x = -1;
  bound_scissor.y = -1;
  bound_scissor.w = -1;
  bound_scissor.h = -1;
  int bound_pipeline = -1;
  SDL_GPUTexture *bound_texture = nullptr;
  auto bindPipeline = [&](int pipeline, SDL_GPUBuffer *buffer, SDL_GPUGraphicsPipeline *gpu) {
    if (bound_pipeline == pipeline) return;
    SDL_BindGPUGraphicsPipeline(pass, gpu);
    SDL_GPUBufferBinding binding;
    binding.buffer = buffer;
    binding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    bound_pipeline = pipeline;
    bound_texture = nullptr;  // a new pipeline invalidates the bound sampler
  };
  auto bindTexture = [&](SDL_GPUTexture *texture) {
    if (bound_texture == texture) return;
    SDL_GPUTextureSamplerBinding texture_binding;
    texture_binding.texture = texture;
    texture_binding.sampler = g_sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, &texture_binding, 1);
    bound_texture = texture;
  };
  for (size_t i = begin; i < end && i < runs.size(); i++) {
    const XtGuiDrawRun &run = runs[i];
    if (run.pipeline == XtGuiDrawRun::LayerBegin || run.pipeline == XtGuiDrawRun::LayerEnd)
      continue;
    if (run.count == 0) continue;
    SDL_Rect scissor;
    scissor.x = 0;
    scissor.y = 0;
    scissor.w = target_width;
    scissor.h = target_height;
    if (run.clip_width > 0.0f && run.clip_height > 0.0f) {
      int x0 = (int)(run.clip_x * pixel_density);
      int y0 = (int)(run.clip_y * pixel_density);
      int x1 = (int)((run.clip_x + run.clip_width) * pixel_density + 0.5f);
      int y1 = (int)((run.clip_y + run.clip_height) * pixel_density + 0.5f);
      if (x0 < 0) x0 = 0;
      if (y0 < 0) y0 = 0;
      if (x1 > target_width) x1 = target_width;
      if (y1 > target_height) y1 = target_height;
      scissor.x = x0;
      scissor.y = y0;
      scissor.w = x1 > x0 ? x1 - x0 : 0;
      scissor.h = y1 > y0 ? y1 - y0 : 0;
    }
    if (scissor.x != bound_scissor.x || scissor.y != bound_scissor.y ||
        scissor.w != bound_scissor.w || scissor.h != bound_scissor.h) {
      SDL_SetGPUScissor(pass, &scissor);
      bound_scissor = scissor;
    }

    if (run.pipeline == XtGuiDrawRun::Shape) {
      if (g_pipeline == nullptr || shapes->buffer == nullptr) continue;
      bindPipeline(XtGuiDrawRun::Shape, shapes->buffer, g_pipeline);
      SDL_DrawGPUPrimitives(pass, run.count, 1, run.first, 0);
      xtgui::g_perf_frame.drawCalls += 1;
    } else if (run.pipeline == XtGuiDrawRun::Text) {
      if (!g_text_ok || g_atlas_texture == nullptr || texts->buffer == nullptr) continue;
      bindPipeline(XtGuiDrawRun::Text, texts->buffer, g_text_pipeline);
      bindTexture(g_atlas_texture);
      SDL_DrawGPUPrimitives(pass, run.count, 1, run.first, 0);
      xtgui::g_perf_frame.drawCalls += 1;
    } else if (run.pipeline == XtGuiDrawRun::Image) {
      if (g_image_pipeline == nullptr || g_sampler == nullptr || images->buffer == nullptr ||
          run.texture == nullptr)
        continue;
      bindPipeline(XtGuiDrawRun::Image, images->buffer, g_image_pipeline);
      bindTexture(run.texture);
      SDL_DrawGPUPrimitives(pass, run.count, 1, run.first, 0);
      xtgui::g_perf_frame.drawCalls += 1;
    }
  }

  /* Restore the full-target scissor so the next pass starts clean. */
  SDL_Rect full;
  full.x = 0;
  full.y = 0;
  full.w = target_width;
  full.h = target_height;
  SDL_SetGPUScissor(pass, &full);
}

}  // namespace renderer_detail
}  // namespace xtgui

void xt_gui_renderer_draw(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *swapchain,
                          const float clear_color[4], XtGuiGeometry *shapes,
                          XtGuiGeometry *texts, XtGuiGeometry *images,
                          const xtgui::DisplayList &list, float viewport_width,
                          float viewport_height, float pixel_density) {
  using namespace xtgui::renderer_detail;
  if (cmd == nullptr || swapchain == nullptr) return;
  if (viewport_width <= 0.0f || viewport_height <= 0.0f) return;
  if (pixel_density <= 0.0f) pixel_density = 1.0f;
  float viewport[2] = {viewport_width, viewport_height};
  SDL_PushGPUVertexUniformData(cmd, 0, viewport, sizeof(viewport));

  /* `mix-blend-mode` needs the backdrop as a texture; the blend pipeline is
   * compiled at run time on the SPIR-V/MSL backends. Everywhere else (or if
   * that compilation failed) the ordinary single pass still paints the
   * unblended content rather than dropping it. */
  if (!list.layers.empty() && layerRendererReady(g_device)) {
    if (drawLayeredScene(cmd, swapchain, clear_color, shapes, texts, images, list, viewport_width,
                         viewport_height, pixel_density))
      return;
  }

  SDL_GPUColorTargetInfo target;
  memset(&target, 0, sizeof(target));
  target.texture = swapchain;
  target.clear_color.r = clear_color != nullptr ? clear_color[0] : 0.0f;
  target.clear_color.g = clear_color != nullptr ? clear_color[1] : 0.0f;
  target.clear_color.b = clear_color != nullptr ? clear_color[2] : 0.0f;
  target.clear_color.a = clear_color != nullptr ? clear_color[3] : 1.0f;
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
  if (pass == nullptr) return;
  int target_width = (int)(viewport_width * pixel_density + 0.5f);
  int target_height = (int)(viewport_height * pixel_density + 0.5f);
  if (shapes != nullptr && texts != nullptr && images != nullptr) {
    drawRuns(pass, shapes->runs, 0, shapes->runs.size(), shapes, texts, images, target_width,
             target_height, pixel_density);
  }
  SDL_EndGPURenderPass(pass);
}
