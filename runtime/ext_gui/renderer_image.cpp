/*
 * xbintsc GUI engine — SDL_GPU 2D renderer: image textures and geometry.
 */

#include "renderer_internal.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "canvas.h"
#include "image.h"
#include "image_api.h"
#include "media.h"
#include "perf.h"
#include "svg.h"
#include "webgl_api.h"

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
  /** Staging transfer buffer reused by every upload into this texture. */
  SDL_GPUTransferBuffer *staging = nullptr;
  uint32_t staging_capacity = 0;
};

static std::unordered_map<uint64_t, MediaTexture> g_media_textures;

/** One live `<canvas>` texture, keyed by the canvas id from `canvas.h`. It is
 * re-uploaded in place whenever the canvas bumps its `generation`; only the
 * canvas's dirty rectangle is copied. */
static std::unordered_map<uint64_t, MediaTexture> g_canvas_textures;

static void releaseStaging(SDL_GPUDevice *device, MediaTexture &entry) {
  if (entry.staging != nullptr) SDL_ReleaseGPUTransferBuffer(device, entry.staging);
  entry.staging = nullptr;
  entry.staging_capacity = 0;
}

/** Upload the device-space rectangle `(x, y, w, h)` of an RGBA8 image into
 * `texture`, through `entry`'s reused staging buffer. Because command buffers
 * run in submission order and this copy is submitted before the frame that
 * samples the texture, re-uploading a texture the previous frame drew is
 * safe. With a batch the copy joins the frame's upload command buffer. */
static bool uploadPixels(SDL_GPUDevice *device, MediaTexture &entry, const void *pixels,
                         int image_width, int x, int y, int w, int h,
                         XtGuiUploadBatch *batch) {
  if (entry.texture == nullptr || w <= 0 || h <= 0) return false;
  const uint32_t bytes = (uint32_t)w * (uint32_t)h * 4u;
  if (bytes == 0) return false;
  if (entry.staging == nullptr || entry.staging_capacity < bytes) {
    releaseStaging(device, entry);
    SDL_GPUTransferBufferCreateInfo transfer_info;
    memset(&transfer_info, 0, sizeof(transfer_info));
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = bytes;
    entry.staging = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    entry.staging_capacity = entry.staging != nullptr ? bytes : 0;
    if (entry.staging == nullptr) return false;
  }
  /* `cycle` rotates SDL's internal allocation while the previous frame's copy
   * may still be in flight. */
  void *mapped = SDL_MapGPUTransferBuffer(device, entry.staging, true);
  if (mapped == nullptr) return false;
  const unsigned char *source = (const unsigned char *)pixels;
  unsigned char *destination = (unsigned char *)mapped;
  for (int row = 0; row < h; row++) {
    memcpy(destination + (size_t)row * (size_t)w * 4,
           source + ((size_t)(y + row) * (size_t)image_width + (size_t)x) * 4,
           (size_t)w * 4);
  }
  SDL_UnmapGPUTransferBuffer(device, entry.staging);

  SDL_GPUCommandBuffer *cmd = xtgui::renderer_detail::uploadCommand(device, batch);
  if (cmd == nullptr) return false;
  SDL_GPUTextureTransferInfo source_info;
  memset(&source_info, 0, sizeof(source_info));
  source_info.transfer_buffer = entry.staging;
  source_info.offset = 0;
  source_info.pixels_per_row = (Uint32)w;
  source_info.rows_per_layer = (Uint32)h;
  SDL_GPUTextureRegion destination_region;
  memset(&destination_region, 0, sizeof(destination_region));
  destination_region.texture = entry.texture;
  destination_region.x = (Uint32)x;
  destination_region.y = (Uint32)y;
  destination_region.mip_level = 0;
  destination_region.layer = 0;
  destination_region.w = (Uint32)w;
  destination_region.h = (Uint32)h;
  destination_region.d = 1;
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_UploadToGPUTexture(copy, &source_info, &destination_region, false);
  SDL_EndGPUCopyPass(copy);
  xtgui::renderer_detail::uploadDone(cmd, batch);
  return true;
}

/** One-shot full-texture upload (image/SVG decode), used when there is no
 * long-lived texture entry to keep a staging buffer on. */
static bool uploadWholeTexture(SDL_GPUDevice *device, SDL_GPUTexture *texture, int width,
                               int height, const void *pixels, XtGuiUploadBatch *batch) {
  MediaTexture temporary;
  temporary.texture = texture;
  const bool ok = uploadPixels(device, temporary, pixels, width, 0, 0, width, height, batch);
  releaseStaging(device, temporary);
  return ok;
}

/** Ensure the texture for `media_id` exists and holds the decoder's newest
 * frame. Returns `nullptr` before the first frame is decoded. */
static SDL_GPUTexture *ensureMediaTexture(SDL_GPUDevice *device, uint64_t media_id,
                                          XtGuiUploadBatch *batch) {
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
    if (uploadPixels(device, entry, image->pixels.data(), image->width, 0, 0, entry.width,
                     entry.height, batch)) {
      entry.generation = player->frame_generation();
    }
  }
  return entry.texture;
}

/** Ensure the texture for `canvas_id` exists and holds the canvas's newest
 * pixels. A WebGL canvas already owns its colour target, so it is returned
 * directly and nothing is copied through the CPU. */
static SDL_GPUTexture *ensureCanvasTexture(SDL_GPUDevice *device, uint64_t canvas_id,
                                          XtGuiUploadBatch *batch) {
  if (const XtWebglApi *webgl = xt_webgl_api()) {
    if (void *texture = webgl->color_texture(canvas_id)) {
      return static_cast<SDL_GPUTexture *>(texture);
    }
  }
  xtgui::Canvas2D *canvas = xtgui::xt_canvas_by_id(canvas_id);
  if (canvas == nullptr) return nullptr;
  const xtgui::Image &image = canvas->image();
  if (image.width <= 0 || image.height <= 0) return nullptr;
  MediaTexture &entry = g_canvas_textures[canvas_id];
  if (entry.texture == nullptr || entry.width != image.width || entry.height != image.height) {
    if (entry.texture != nullptr) SDL_ReleaseGPUTexture(device, entry.texture);
    SDL_GPUTextureCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = (Uint32)image.width;
    info.height = (Uint32)image.height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    entry.texture = SDL_CreateGPUTexture(device, &info);
    entry.width = image.width;
    entry.height = image.height;
    entry.generation = ~0ull;
    if (entry.texture == nullptr) return nullptr;
  }
  if (entry.generation == canvas->generation()) return entry.texture;
  const xtgui::Canvas2D::CanvasDirtyRect &dirty = canvas->pending_dirty();
  /* A draw that changed no pixel (off canvas, zero coverage) must not pay for
   * an upload: remember the generation and skip it. */
  if (dirty.empty()) {
    entry.generation = canvas->generation();
    return entry.texture;
  }
  /* Capture the rectangle before `clear_dirty()` resets it. */
  const int dirty_w = dirty.x1 - dirty.x0;
  const int dirty_h = dirty.y1 - dirty.y0;
  if (uploadPixels(device, entry, image.pixels.data(), image.width, dirty.x0, dirty.y0, dirty_w,
                   dirty_h, batch)) {
    entry.generation = canvas->generation();
    canvas->clear_dirty();
    xtgui::g_perf_frame.canvasUploads += 1;
    xtgui::g_perf_frame.canvasUploadBytes +=
        (uint64_t)dirty_w * (uint64_t)dirty_h * 4u;
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

/** Create an RGBA texture holding `image` and upload its pixels. Returns null
 * (after reporting the SDL error) when either step fails. */
SDL_GPUTexture *createImageTexture(SDL_GPUDevice *device, const xtgui::Image &image,
                                   XtGuiUploadBatch *batch) {
  SDL_GPUTextureCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = (Uint32)image.width;
  info.height = (Uint32)image.height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &info);
  if (texture == nullptr) {
    fprintf(stderr, "xt_gui: image texture creation failed: %s\n", SDL_GetError());
    return nullptr;
  }
  if (!uploadWholeTexture(device, texture, image.width, image.height, image.pixels.data(), batch)) {
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }
  return texture;
}

/** Decode `src` (cached) and upload it as an RGBA texture. Vector sources are
 * rasterised at the drawn pixel size and cached by that size; bitmaps decode at
 * their intrinsic size and are cached by path. */
SDL_GPUTexture *ensureImageTexture(SDL_GPUDevice *device, const std::string &src, int pixel_width,
                                   int pixel_height, XtGuiUploadBatch *batch) {
  const XtImageApi *images = xt_image_api();
  if (images == nullptr) return nullptr;
  bool vector_source = images->is_vector(src);
  std::string key =
      vector_source ? src + "@" + std::to_string(pixel_width) + "x" + std::to_string(pixel_height)
                    : src;
  auto found = g_image_textures.find(key);
  if (found != g_image_textures.end()) return found->second;

  const xtgui::Image *image = images->load(src, pixel_width, pixel_height);
  if (image == nullptr || image->width <= 0 || image->height <= 0) {
    fprintf(stderr, "xt_gui: could not decode image '%s'\n", src.c_str());
    g_image_textures.emplace(key, nullptr);
    return nullptr;
  }

  SDL_GPUTexture *texture = createImageTexture(device, *image, batch);
  g_image_textures.emplace(key, texture);
  return texture;
}

/** Rasterise the serialized subtree of an inline `<svg>` at the drawn pixel
 * size and upload it. The markup is the cache key: a static document therefore
 * rasterises once per drawn size, and an edited one gets a fresh texture. */
SDL_GPUTexture *ensureInlineSvgTexture(SDL_GPUDevice *device, const std::string &markup,
                                       int pixel_width, int pixel_height,
                                       XtGuiUploadBatch *batch) {
  std::string key = "inline-svg@" + std::to_string(pixel_width) + "x" +
                    std::to_string(pixel_height) + ":" + markup;
  auto found = g_image_textures.find(key);
  if (found != g_image_textures.end()) return found->second;

  const XtImageApi *images = xt_image_api();
  if (images == nullptr) return nullptr;
  xtgui::Image image;
  if (!images->svg_markup_render(markup, pixel_width, pixel_height, &image)) {
    fprintf(stderr, "xt_gui: could not rasterise inline SVG\n");
    g_image_textures.emplace(key, nullptr);
    return nullptr;
  }

  SDL_GPUTexture *texture = createImageTexture(device, image, batch);
  g_image_textures.emplace(key, texture);
  return texture;
}

}  // namespace renderer_detail
}  // namespace xtgui

void xt_gui_image_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                         const xtgui::DisplayList &list, float scale, XtGuiUploadBatch *batch) {
  using namespace xtgui::renderer_detail;
  geometry->op_counts.assign(list.images.size(), 0);
  geometry->op_textures.assign(list.images.size(), nullptr);
  if (g_image_pipeline == nullptr) {
    geometry->vertex_count = 0;
    return;
  }
  if (scale <= 0.0f) scale = 1.0f;
  std::vector<TextVertex> &verts = g_image_staging;
  verts.clear();
  verts.reserve(list.images.size() * 6);
  GlyphEntry full;
  full.u0 = 0.0f;
  full.v0 = 0.0f;
  full.u1 = 1.0f;
  full.v1 = 1.0f;
  beginGradientUpload();
  beginBitmapUpload();
  for (const xtgui::PaintOp &op : list.ops) {
    if (op.kind != xtgui::PaintKind::Image || op.index >= list.images.size()) continue;
    const xtgui::PaintImage &image = list.images[op.index];
    if (image.width <= 0.0f || image.height <= 0.0f) continue;
    /* Draw-time device pixels: an SVG (file or inline) is rasterised at this
     * size so it stays crisp on HiDPI and when scaled. */
    int pixel_width = (int)(image.width * scale + 0.5f);
    int pixel_height = (int)(image.height * scale + 0.5f);
    if (pixel_width < 1) pixel_width = 1;
    if (pixel_height < 1) pixel_height = 1;
    SDL_GPUTexture *texture = nullptr;
    if (image.bitmap != nullptr) {
      texture = ensureBitmapTexture(device, *image.bitmap, batch);
    } else if (image.gradient != nullptr) {
      texture = ensureGradientTexture(device, image, pixel_width, pixel_height, batch);
    } else if (image.canvas_id != 0) {
      texture = ensureCanvasTexture(device, image.canvas_id, batch);
    } else if (image.media_id != 0) {
      texture = ensureMediaTexture(device, image.media_id, batch);
    } else if (!image.svg.empty()) {
      texture = ensureInlineSvgTexture(device, image.svg, pixel_width, pixel_height, batch);
    } else {
      texture = ensureImageTexture(device, image.src, pixel_width, pixel_height, batch);
    }
    if (texture == nullptr) continue;
    uint32_t count = pushGlyph(verts, image.x, image.y, image.width, image.height, full,
                               image.tint, image.transform, op.clip, list);
    geometry->op_counts[op.index] = count;
    geometry->op_textures[op.index] = texture;
  }
  finishGradientUpload(device);
  finishBitmapUpload(device);
  uploadVertices(device, geometry, verts.data(), (uint32_t)verts.size(), sizeof(TextVertex), batch);
}

void xt_gui_image_textures_destroy(SDL_GPUDevice *device) {
  using namespace xtgui::renderer_detail;
  releaseGradientTextures(device);
  releaseBitmapTextures(device);
  for (auto &entry : g_image_textures) {
    if (entry.second != nullptr) SDL_ReleaseGPUTexture(device, entry.second);
  }
  g_image_textures.clear();
  for (auto &entry : g_media_textures) {
    if (entry.second.texture != nullptr) SDL_ReleaseGPUTexture(device, entry.second.texture);
    releaseStaging(device, entry.second);
  }
  g_media_textures.clear();
  for (auto &entry : g_canvas_textures) {
    if (entry.second.texture != nullptr) SDL_ReleaseGPUTexture(device, entry.second.texture);
    releaseStaging(device, entry.second);
  }
  g_canvas_textures.clear();
}

bool xt_gui_media_upload_frames(SDL_GPUDevice *device, XtGuiUploadBatch *batch) {
  using namespace xtgui::renderer_detail;
  bool uploaded = false;
  for (auto &entry : g_media_textures) {
    const uint64_t before = entry.second.generation;
    ensureMediaTexture(device, entry.first, batch);
    uploaded = uploaded || entry.second.generation != before;
  }
  return uploaded;
}

void xt_gui_media_texture_release(SDL_GPUDevice *device, uint64_t media_id) {
  using namespace xtgui::renderer_detail;
  auto it = g_media_textures.find(media_id);
  if (it == g_media_textures.end()) return;
  if (it->second.texture != nullptr) SDL_ReleaseGPUTexture(device, it->second.texture);
  releaseStaging(device, it->second);
  g_media_textures.erase(it);
}

bool xt_gui_canvas_upload_frames(SDL_GPUDevice *device, XtGuiUploadBatch *batch) {
  using namespace xtgui::renderer_detail;
  bool uploaded = false;
  for (auto &entry : g_canvas_textures) {
    const uint64_t before = xtgui::g_perf_frame.canvasUploads;
    ensureCanvasTexture(device, entry.first, batch);
    uploaded = uploaded || xtgui::g_perf_frame.canvasUploads != before;
  }
  return uploaded;
}

void xt_gui_canvas_texture_release(SDL_GPUDevice *device, uint64_t canvas_id) {
  using namespace xtgui::renderer_detail;
  auto it = g_canvas_textures.find(canvas_id);
  if (it == g_canvas_textures.end()) return;
  if (it->second.texture != nullptr) SDL_ReleaseGPUTexture(device, it->second.texture);
  releaseStaging(device, it->second);
  g_canvas_textures.erase(it);
}
