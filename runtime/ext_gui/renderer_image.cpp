/*
 * xbintsc GUI engine — SDL_GPU 2D renderer: image textures and geometry.
 */

#include "renderer_internal.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "image.h"

#include "renderer_shaders.h"

namespace xtgui {
namespace renderer_detail {

SDL_GPUGraphicsPipeline *createImagePipeline(SDL_GPUDevice *device, SDL_Window *reference) {
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
  fs_info.entrypoint = "fs_image";
  fs_info.format = SDL_GPU_SHADERFORMAT_MSL;
  fs_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  fs_info.num_samplers = 1;
  SDL_GPUShader *fs = SDL_CreateGPUShader(device, &fs_info);

  if (vs == nullptr || fs == nullptr) {
    fprintf(stderr, "xt_gui: image shader creation failed: %s\n", SDL_GetError());
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
    fprintf(stderr, "xt_gui: image pipeline creation failed: %s\n", SDL_GetError());
  }
  return pipeline;
}

/** Decode `src` (cached) and upload it as an RGBA texture (cached by path). */
SDL_GPUTexture *ensureImageTexture(SDL_GPUDevice *device, const std::string &src) {
  auto found = g_image_textures.find(src);
  if (found != g_image_textures.end()) return found->second;

  const xtgui::Image *image = xtgui::xt_image_load(src);
  if (image == nullptr || image->width <= 0 || image->height <= 0) {
    fprintf(stderr, "xt_gui: could not decode image '%s'\n", src.c_str());
    g_image_textures.emplace(src, nullptr);
    return nullptr;
  }

  SDL_GPUTextureCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = (Uint32)image->width;
  info.height = (Uint32)image->height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &info);
  if (texture == nullptr) {
    fprintf(stderr, "xt_gui: image texture creation failed: %s\n", SDL_GetError());
    g_image_textures.emplace(src, nullptr);
    return nullptr;
  }

  uint32_t bytes = (uint32_t)(image->pixels.size());
  SDL_GPUTransferBufferCreateInfo transfer_info;
  memset(&transfer_info, 0, sizeof(transfer_info));
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_info.size = bytes;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
  if (transfer == nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
    g_image_textures.emplace(src, nullptr);
    return nullptr;
  }
  void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  std::memcpy(mapped, image->pixels.data(), bytes);
  SDL_UnmapGPUTransferBuffer(device, transfer);

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  if (cmd == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, texture);
    g_image_textures.emplace(src, nullptr);
    return nullptr;
  }
  SDL_GPUTextureTransferInfo source;
  memset(&source, 0, sizeof(source));
  source.transfer_buffer = transfer;
  source.offset = 0;
  source.pixels_per_row = (Uint32)image->width;
  source.rows_per_layer = (Uint32)image->height;
  SDL_GPUTextureRegion destination;
  memset(&destination, 0, sizeof(destination));
  destination.texture = texture;
  destination.mip_level = 0;
  destination.layer = 0;
  destination.x = 0;
  destination.y = 0;
  destination.z = 0;
  destination.w = (Uint32)image->width;
  destination.h = (Uint32)image->height;
  destination.d = 1;
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_UploadToGPUTexture(copy, &source, &destination, false);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(device, transfer);

  g_image_textures.emplace(src, texture);
  return texture;
}

}  // namespace renderer_detail
}  // namespace xtgui

void xt_gui_image_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                         const xtgui::DisplayList &list) {
  using namespace xtgui::renderer_detail;
  g_image_batches.clear();
  if (g_image_pipeline == nullptr) {
    geometry->vertex_count = 0;
    return;
  }
  std::vector<TextVertex> verts;
  verts.reserve(list.images.size() * 6);
  xtgui::Color white{1.0f, 1.0f, 1.0f, 1.0f};
  GlyphEntry full;
  full.u0 = 0.0f;
  full.v0 = 0.0f;
  full.u1 = 1.0f;
  full.v1 = 1.0f;
  for (const xtgui::PaintImage &image : list.images) {
    if (image.width <= 0.0f || image.height <= 0.0f) continue;
    SDL_GPUTexture *texture = ensureImageTexture(device, image.src);
    if (texture == nullptr) continue;
    uint32_t first = (uint32_t)verts.size();
    pushGlyph(verts, image.x, image.y, image.width, image.height, full, white);
    g_image_batches.push_back(ImageBatch{texture, first, 6});
  }
  uploadVertices(device, geometry, verts.data(), (uint32_t)verts.size(), sizeof(TextVertex));
}

void xt_gui_image_draw(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass, XtGuiGeometry *geometry,
                       float viewport_width, float viewport_height) {
  using namespace xtgui::renderer_detail;
  if (g_image_pipeline == nullptr || g_sampler == nullptr) return;
  if (geometry->buffer == nullptr || geometry->vertex_count == 0 || g_image_batches.empty()) return;
  if (viewport_width <= 0.0f || viewport_height <= 0.0f) return;
  SDL_BindGPUGraphicsPipeline(pass, g_image_pipeline);
  SDL_GPUBufferBinding binding;
  binding.buffer = geometry->buffer;
  binding.offset = 0;
  SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
  float viewport[2] = {viewport_width, viewport_height};
  SDL_PushGPUVertexUniformData(cmd, 0, viewport, sizeof(viewport));
  for (const ImageBatch &batch : g_image_batches) {
    if (batch.texture == nullptr) continue;
    SDL_GPUTextureSamplerBinding texture_binding;
    texture_binding.texture = batch.texture;
    texture_binding.sampler = g_sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, &texture_binding, 1);
    SDL_DrawGPUPrimitives(pass, batch.count, 1, batch.first, 0);
  }
}

void xt_gui_image_textures_destroy(SDL_GPUDevice *device) {
  using namespace xtgui::renderer_detail;
  for (auto &entry : g_image_textures) {
    if (entry.second != nullptr) SDL_ReleaseGPUTexture(device, entry.second);
  }
  g_image_textures.clear();
  g_image_batches.clear();
}
