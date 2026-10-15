/*
 * xbintsc GUI engine — SDL_GPU 2D renderer: image textures and geometry.
 */

#include "renderer_internal.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "image.h"
#include "media.h"

#include "renderer_shaders.h"

namespace xtgui {
namespace renderer_detail {

/** One live `<video>` texture. The texture is created once per pixel size and
 * re-uploaded in place whenever the decoder advances (`generation`). */
struct MediaTexture {
  SDL_GPUTexture *texture = nullptr;
  int width = 0;
  int height = 0;
  uint64_t generation = ~0ull;
};

static std::unordered_map<uint64_t, MediaTexture> g_media_textures;

/** Upload RGBA8 `pixels` into `texture` through a staging buffer, submitting
 * its own command buffer. Because command buffers run in submission order and
 * this one is submitted before the frame that samples the texture, re-uploading
 * a texture that the previous frame drew is safe. */
static bool uploadPixels(SDL_GPUDevice *device, SDL_GPUTexture *texture, int width, int height,
                         const void *pixels, uint32_t bytes) {
  if (bytes == 0) return false;
  SDL_GPUTransferBufferCreateInfo transfer_info;
  memset(&transfer_info, 0, sizeof(transfer_info));
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_info.size = bytes;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
  if (transfer == nullptr) return false;
  void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  std::memcpy(mapped, pixels, bytes);
  SDL_UnmapGPUTransferBuffer(device, transfer);

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  if (cmd == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return false;
  }
  SDL_GPUTextureTransferInfo source;
  memset(&source, 0, sizeof(source));
  source.transfer_buffer = transfer;
  source.offset = 0;
  source.pixels_per_row = (Uint32)width;
  source.rows_per_layer = (Uint32)height;
  SDL_GPUTextureRegion destination;
  memset(&destination, 0, sizeof(destination));
  destination.texture = texture;
  destination.mip_level = 0;
  destination.layer = 0;
  destination.w = (Uint32)width;
  destination.h = (Uint32)height;
  destination.d = 1;
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_UploadToGPUTexture(copy, &source, &destination, false);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  return true;
}

/** Ensure the texture for `media_id` exists and holds the decoder's newest
 * frame. Returns `nullptr` before the first frame is decoded. */
static SDL_GPUTexture *ensureMediaTexture(SDL_GPUDevice *device, uint64_t media_id) {
  xtgui::MediaPlayer *player = xtgui::xt_media_by_id(media_id);
  if (player == nullptr) return nullptr;
  const xtgui::Image *image = player->frame();
  MediaTexture &entry = g_media_textures[media_id];
  if (image == nullptr || image->width <= 0 || image->height <= 0) return entry.texture;
  if (entry.texture == nullptr || entry.width != image->width || entry.height != image->height) {
    if (entry.texture != nullptr) SDL_ReleaseGPUTexture(device, entry.texture);
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
    entry.texture = SDL_CreateGPUTexture(device, &info);
    entry.width = image->width;
    entry.height = image->height;
    entry.generation = ~0ull;
    if (entry.texture == nullptr) return nullptr;
  }
  if (entry.generation != player->frame_generation()) {
    if (uploadPixels(device, entry.texture, entry.width, entry.height, image->pixels.data(),
                     (uint32_t)image->pixels.size())) {
      entry.generation = player->frame_generation();
    }
  }
  return entry.texture;
}

SDL_GPUGraphicsPipeline *createImagePipeline(SDL_GPUDevice *device, SDL_Window *reference) {
  const ShaderSource image_vs = selectShader(device, ShaderProgram::Image, false);
  const ShaderSource image_fs = selectShader(device, ShaderProgram::Image, true);
  if (!image_vs.valid() || !image_fs.valid()) return nullptr;

  SDL_GPUShaderCreateInfo vs_info;
  memset(&vs_info, 0, sizeof(vs_info));
  vs_info.code_size = image_vs.size;
  vs_info.code = image_vs.code;
  vs_info.entrypoint = image_vs.entry;
  vs_info.format = image_vs.format;
  vs_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
  vs_info.num_uniform_buffers = 1;
  SDL_GPUShader *vs = SDL_CreateGPUShader(device, &vs_info);

  SDL_GPUShaderCreateInfo fs_info;
  memset(&fs_info, 0, sizeof(fs_info));
  fs_info.code_size = image_fs.size;
  fs_info.code = image_fs.code;
  fs_info.entrypoint = image_fs.entry;
  fs_info.format = image_fs.format;
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

/** Decode `src` (cached) and upload it as an RGBA texture. Vector sources are
 * rasterised at the drawn pixel size and cached by that size; bitmaps decode at
 * their intrinsic size and are cached by path. */
SDL_GPUTexture *ensureImageTexture(SDL_GPUDevice *device, const std::string &src, int pixel_width,
                                   int pixel_height) {
  bool vector_source = xtgui::xt_image_is_vector(src);
  std::string key =
      vector_source ? src + "@" + std::to_string(pixel_width) + "x" + std::to_string(pixel_height)
                    : src;
  auto found = g_image_textures.find(key);
  if (found != g_image_textures.end()) return found->second;

  const xtgui::Image *image = xtgui::xt_image_load(src, pixel_width, pixel_height);
  if (image == nullptr || image->width <= 0 || image->height <= 0) {
    fprintf(stderr, "xt_gui: could not decode image '%s'\n", src.c_str());
    g_image_textures.emplace(key, nullptr);
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
    g_image_textures.emplace(key, nullptr);
    return nullptr;
  }

  uint32_t bytes = (uint32_t)(image->pixels.size());
  if (!uploadPixels(device, texture, image->width, image->height, image->pixels.data(), bytes)) {
    SDL_ReleaseGPUTexture(device, texture);
    g_image_textures.emplace(key, nullptr);
    return nullptr;
  }

  g_image_textures.emplace(key, texture);
  return texture;
}

}  // namespace renderer_detail
}  // namespace xtgui

void xt_gui_image_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                         const xtgui::DisplayList &list, float scale) {
  using namespace xtgui::renderer_detail;
  g_image_batches.clear();
  if (g_image_pipeline == nullptr) {
    geometry->vertex_count = 0;
    return;
  }
  if (scale <= 0.0f) scale = 1.0f;
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
    SDL_GPUTexture *texture = nullptr;
    if (image.media_id != 0) {
      texture = ensureMediaTexture(device, image.media_id);
    } else {
      /* Draw-time device pixels: an SVG is rasterised at this size so it stays
       * crisp on HiDPI and when scaled. */
      int pixel_width = (int)(image.width * scale + 0.5f);
      int pixel_height = (int)(image.height * scale + 0.5f);
      if (pixel_width < 1) pixel_width = 1;
      if (pixel_height < 1) pixel_height = 1;
      texture = ensureImageTexture(device, image.src, pixel_width, pixel_height);
    }
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
  for (auto &entry : g_media_textures) {
    if (entry.second.texture != nullptr) SDL_ReleaseGPUTexture(device, entry.second.texture);
  }
  g_media_textures.clear();
}

void xt_gui_media_upload_frames(SDL_GPUDevice *device) {
  using namespace xtgui::renderer_detail;
  for (auto &entry : g_media_textures) ensureMediaTexture(device, entry.first);
}

void xt_gui_media_texture_release(SDL_GPUDevice *device, uint64_t media_id) {
  using namespace xtgui::renderer_detail;
  auto it = g_media_textures.find(media_id);
  if (it == g_media_textures.end()) return;
  if (it->second.texture != nullptr) SDL_ReleaseGPUTexture(device, it->second.texture);
  g_media_textures.erase(it);
}
