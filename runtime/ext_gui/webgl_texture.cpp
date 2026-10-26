/*
 * xbintsc GUI engine — WebGL texture uploads, mipmaps and `readPixels`.
 */

#include "webgl.h"

#include <algorithm>
#include <cstring>

#include "gui_internal.h"

namespace xtgui {
namespace {

bool depth_texture_format(SDL_GPUTextureFormat format) {
  return format == SDL_GPU_TEXTUREFORMAT_D16_UNORM ||
         format == SDL_GPU_TEXTUREFORMAT_D24_UNORM ||
         format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT ||
         format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ||
         format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
}

int texel_bytes(SDL_GPUTextureFormat format) {
  if (format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) return 8;
  if (format == SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT) return 16;
  return 4;
}

/** Upload a rectangle of tightly packed texels into level 0 of `gpu`. `pixels`
 * may be null, which uploads zeros. */
bool upload_region(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *gpu, int x,
                   int y, int width, int height, const uint8_t *pixels, int bytes_per_texel,
                   int layer = 0) {
  const uint32_t bytes = (uint32_t)(width * height * bytes_per_texel);
  if (bytes == 0) return true;
  std::vector<uint8_t> zeros;
  if (pixels == nullptr) {
    zeros.assign(bytes, 0);
    pixels = zeros.data();
  }
  SDL_GPUTransferBufferCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  info.size = bytes;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &info);
  if (transfer == nullptr) return false;
  void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  if (mapped == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return false;
  }
  memcpy(mapped, pixels, bytes);
  SDL_UnmapGPUTransferBuffer(device, transfer);
  SDL_GPUTextureTransferInfo source;
  memset(&source, 0, sizeof(source));
  source.transfer_buffer = transfer;
  source.pixels_per_row = (Uint32)width;
  source.rows_per_layer = (Uint32)height;
  SDL_GPUTextureRegion region;
  memset(&region, 0, sizeof(region));
  region.texture = gpu;
  region.x = (Uint32)x;
  region.y = (Uint32)y;
  region.layer = (Uint32)layer;
  region.w = (Uint32)width;
  region.h = (Uint32)height;
  region.d = 1;
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_UploadToGPUTexture(copy, &source, &region, false);
  SDL_EndGPUCopyPass(copy);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  return true;
}

/** Upload a whole 3D/2D-array volume. Each slice is uploaded separately:
 * SDL's multi-slice transfer stride is not usable for a 2-layer 3D texture on
 * the Metal backend. */
bool upload_volume(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *gpu,
                   int width, int height, int depth, const uint8_t *pixels,
                   int bytes_per_texel, bool is_3d) {
  const uint32_t slice_bytes = (uint32_t)(width * height * bytes_per_texel);
  const uint32_t bytes = slice_bytes * (uint32_t)depth;
  if (bytes == 0) return true;
  std::vector<uint8_t> zeros;
  if (pixels == nullptr) {
    zeros.assign(bytes, 0);
    pixels = zeros.data();
  }
  SDL_GPUTransferBufferCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  info.size = bytes;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &info);
  if (transfer == nullptr) return false;
  void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  if (mapped == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return false;
  }
  memcpy(mapped, pixels, bytes);
  SDL_UnmapGPUTransferBuffer(device, transfer);
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  for (int slice = 0; slice < depth; slice++) {
    SDL_GPUTextureTransferInfo source;
    memset(&source, 0, sizeof(source));
    source.transfer_buffer = transfer;
    source.offset = (Uint32)slice * slice_bytes;
    source.pixels_per_row = (Uint32)width;
    source.rows_per_layer = (Uint32)height;
    SDL_GPUTextureRegion region;
    memset(&region, 0, sizeof(region));
    region.texture = gpu;
    region.layer = is_3d ? 0 : (Uint32)slice;
    region.z = is_3d ? (Uint32)slice : 0;
    region.w = (Uint32)width;
    region.h = (Uint32)height;
    region.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &region, false);
  }
  SDL_EndGPUCopyPass(copy);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  return true;
}

}  // namespace

SDL_GPUTexture *WebGLContext::clear_color_texture() {
  if (guidev::g_device == nullptr) return nullptr;
  if (clear_texture_ == nullptr) {
    end_pass();
    ensure_command();
    SDL_GPUTextureCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = 1;
    info.height = 1;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    clear_texture_ = SDL_CreateGPUTexture(guidev::g_device, &info);
    if (clear_texture_ == nullptr) {
      set_error(GL_OUT_OF_MEMORY);
      return nullptr;
    }
  }
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return clear_texture_;
  uint8_t rgba[4];
  for (int i = 0; i < 4; i++) {
    float value = clear_color_[i];
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    rgba[i] = (uint8_t)(value * 255.0f + 0.5f);
  }
  upload_region(guidev::g_device, cmd_, clear_texture_, 0, 0, 1, 1, rgba, 4);
  return clear_texture_;
}

bool WebGLContext::tex_image_format(uint32_t id, int width, int height,
                                    SDL_GPUTextureFormat format, const uint8_t *pixels,
                                    int layer) {
  WebGLTexture *record = texture(id);
  if (record == nullptr || width <= 0 || height <= 0) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return false;
  if (record->width != width || record->height != height || record->format != format) {
    if (record->gpu != nullptr) {
      SDL_WaitForGPUIdle(guidev::g_device);
      SDL_ReleaseGPUTexture(guidev::g_device, record->gpu);
      record->gpu = nullptr;
      record->usage = 0;
    }
    record->width = width;
    record->height = height;
    record->format = format;
    record->layers = 1;
    record->target = layer >= 0 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    record->compressed = false;
    record->has_mipmaps = false;
    if (record->sampler != nullptr) {
      SDL_ReleaseGPUSampler(guidev::g_device, record->sampler);
      record->sampler = nullptr;
    }
  }
  if (ensure_texture_gpu(record, record->as_target || record->as_depth_target) == nullptr) {
    return false;
  }
  record->defined = true;
  if (layer >= 0) {
    record->target = GL_TEXTURE_CUBE_MAP;
    record->layers = 6;
  }
  return upload_region(guidev::g_device, cmd_, record->gpu, 0, 0, width, height, pixels,
                       texel_bytes(format), layer >= 0 ? layer : 0);
}

bool WebGLContext::tex_image_rgba(uint32_t id, int width, int height, const uint8_t *rgba) {
  return tex_image_format(id, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, rgba);
}

bool WebGLContext::tex_sub_image_format(uint32_t id, int x, int y, int width, int height,
                                        const uint8_t *pixels, int layer) {
  WebGLTexture *record = texture(id);
  if (record == nullptr || !record->defined || record->gpu == nullptr) {
    set_error(GL_INVALID_OPERATION);
    return false;
  }
  if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > record->width ||
      y + height > record->height) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return false;
  return upload_region(guidev::g_device, cmd_, record->gpu, x, y, width, height, pixels,
                       texel_bytes(record->format), layer);
}

bool WebGLContext::tex_sub_image_rgba(uint32_t id, int x, int y, int width, int height,
                                      const uint8_t *rgba) {
  return tex_sub_image_format(id, x, y, width, height, rgba);
}

bool WebGLContext::generate_mipmap(uint32_t id) {
  WebGLTexture *record = texture(id);
  if (record == nullptr || record->gpu == nullptr) {
    set_error(GL_INVALID_OPERATION);
    return false;
  }
  if (record->depth) {
    /* Depth textures have no mip chain in this backend. */
    set_error(GL_INVALID_OPERATION);
    return false;
  }
  end_pass();
  ensure_command();
  if (cmd_ == nullptr || record->levels <= 1) return false;
  SDL_GenerateMipmapsForGPUTexture(cmd_, record->gpu);
  record->has_mipmaps = true;
  if (record->sampler != nullptr) {
    SDL_ReleaseGPUSampler(guidev::g_device, record->sampler);
    record->sampler = nullptr;
  }
  return true;
}

bool WebGLContext::read_pixels(int x, int y, int width, int height, uint8_t *out) {
  if (out == nullptr || width <= 0 || height <= 0) return false;
  if (target_color_format() != (int)SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) {
    /* The only readback this surface offers is RGBA8; a float/half colour
     * target is read by rendering it into an RGBA8 target instead. */
    set_error(GL_INVALID_OPERATION);
    return false;
  }
  SDL_GPUTexture *target = target_color();
  if (bound_framebuffer_ != 0) {
    WebGLFramebuffer *framebuffer = this->framebuffer(bound_framebuffer_);
    if (framebuffer != nullptr) {
      int index = read_buffer_ - GL_COLOR_ATTACHMENT0;
      if (index < 0 || index >= kWebGLMaxDrawBuffers ||
          framebuffer->color_textures[index] == 0) {
        index = 0;
      }
      WebGLTexture *read_texture = texture(framebuffer->color_textures[index]);
      target = read_texture != nullptr ? ensure_texture_gpu(read_texture, true) : nullptr;
    }
  }
  int target_width = bound_framebuffer_ == 0 ? width_ : 0;
  int target_height = bound_framebuffer_ == 0 ? height_ : 0;
  if (bound_framebuffer_ != 0) {
    WebGLFramebuffer *framebuffer_ = framebuffer(bound_framebuffer_);
    if (framebuffer_ == nullptr) return false;
    target_width = framebuffer_->width;
    target_height = framebuffer_->height;
  }
  if (target == nullptr || target_width <= 0 || target_height <= 0) return false;
  /* Clamp to the target; pixels outside are transparent black. */
  const int px = std::max(0, x);
  const int py = std::max(0, y);
  const int pw = std::min(width, target_width - px);
  const int ph = std::min(height, target_height - py);
  memset(out, 0, (size_t)width * height * 4);
  if (pw <= 0 || ph <= 0) return true;

  submit();
  if (guidev::g_device == nullptr) return false;
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(guidev::g_device);
  if (cmd == nullptr) return false;
  const uint32_t bytes = (uint32_t)(pw * ph * 4);
  SDL_GPUTransferBufferCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  info.size = bytes;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(guidev::g_device, &info);
  if (transfer == nullptr) {
    SDL_SubmitGPUCommandBuffer(cmd);
    return false;
  }
  SDL_GPUTextureRegion region;
  memset(&region, 0, sizeof(region));
  region.texture = target;
  /* The framebuffer is stored top-down; GL's y counts from the bottom. */
  region.x = (Uint32)px;
  region.y = (Uint32)(target_height - (py + ph));
  region.w = (Uint32)pw;
  region.h = (Uint32)ph;
  region.d = 1;
  SDL_GPUTextureTransferInfo destination;
  memset(&destination, 0, sizeof(destination));
  destination.transfer_buffer = transfer;
  destination.pixels_per_row = (Uint32)pw;
  destination.rows_per_layer = (Uint32)ph;
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_DownloadFromGPUTexture(copy, &region, &destination);
  SDL_EndGPUCopyPass(copy);
  SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (fence == nullptr) {
    SDL_ReleaseGPUTransferBuffer(guidev::g_device, transfer);
    return false;
  }
  SDL_WaitForGPUFences(guidev::g_device, true, &fence, 1);
  SDL_ReleaseGPUFence(guidev::g_device, fence);
  const uint8_t *mapped =
      (const uint8_t *)SDL_MapGPUTransferBuffer(guidev::g_device, transfer, false);
  if (mapped == nullptr) {
    SDL_ReleaseGPUTransferBuffer(guidev::g_device, transfer);
    return false;
  }
  for (int row = 0; row < ph; row++) {
    const uint8_t *source = mapped + (size_t)(ph - 1 - row) * pw * 4;
    uint8_t *line = out + ((size_t)(py - y + row) * width + (px - x)) * 4;
    memcpy(line, source, (size_t)pw * 4);
  }
  SDL_UnmapGPUTransferBuffer(guidev::g_device, transfer);
  SDL_ReleaseGPUTransferBuffer(guidev::g_device, transfer);
  return true;
}

bool WebGLContext::tex_storage_format(uint32_t id, int levels, int width, int height,
                                      SDL_GPUTextureFormat format) {
  (void)levels;  // the backend always allocates the full mip chain
  WebGLTexture *record = texture(id);
  if (record == nullptr || width <= 0 || height <= 0) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  end_pass();
  ensure_command();
  if (record->gpu != nullptr) {
    submit();
    SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, record->gpu);
    record->gpu = nullptr;
    record->usage = 0;
  }
  record->width = width;
  record->height = height;
  record->format = format;
  record->layers = 1;
  record->target = GL_TEXTURE_2D;
  record->compressed = false;
  record->has_mipmaps = false;
  if (record->sampler != nullptr && guidev::g_device != nullptr) {
    SDL_ReleaseGPUSampler(guidev::g_device, record->sampler);
    record->sampler = nullptr;
  }
  record->defined = true;
  return ensure_texture_gpu(record, record->as_target || record->as_depth_target) != nullptr;
}

bool WebGLContext::tex_storage_2d(uint32_t id, int levels, int width, int height) {
  return tex_storage_format(id, levels, width, height,
                            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
}

bool WebGLContext::tex_storage_3d(uint32_t id, int levels, int width, int height, int depth,
                                  SDL_GPUTextureFormat format, int target) {
  (void)levels;
  WebGLTexture *record = texture(id);
  if (record == nullptr || width <= 0 || height <= 0 || depth <= 0) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  end_pass();
  ensure_command();
  if (record->gpu != nullptr) {
    submit();
    SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, record->gpu);
    record->gpu = nullptr;
    record->usage = 0;
  }
  record->width = width;
  record->height = height;
  record->layers = depth;
  record->format = format;
  record->depth = false;
  record->compressed = false;
  record->target = target == GL_TEXTURE_3D ? GL_TEXTURE_3D : GL_TEXTURE_2D_ARRAY;
  record->has_mipmaps = false;
  if (record->sampler != nullptr && guidev::g_device != nullptr) {
    SDL_ReleaseGPUSampler(guidev::g_device, record->sampler);
    record->sampler = nullptr;
  }
  record->defined = true;
  return ensure_texture_gpu(record, false) != nullptr;
}

bool WebGLContext::tex_image_3d(uint32_t id, int width, int height, int depth,
                                SDL_GPUTextureFormat format, const uint8_t *pixels,
                                int target) {
  WebGLTexture *record = texture(id);
  if (record == nullptr || width <= 0 || height <= 0 || depth <= 0) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return false;
  const int resolved_target = target == GL_TEXTURE_3D ? GL_TEXTURE_3D : GL_TEXTURE_2D_ARRAY;
  if (record->gpu != nullptr &&
      (record->width != width || record->height != height || record->layers != depth ||
       record->format != format || record->target != resolved_target)) {
    SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, record->gpu);
    record->gpu = nullptr;
    record->usage = 0;
  }
  record->width = width;
  record->height = height;
  record->layers = depth;
  record->format = format;
  record->depth = false;
  record->compressed = false;
  record->target = resolved_target;
  record->has_mipmaps = false;
  if (record->sampler != nullptr) {
    SDL_ReleaseGPUSampler(guidev::g_device, record->sampler);
    record->sampler = nullptr;
  }
  if (ensure_texture_gpu(record, false) == nullptr) return false;
  record->defined = true;
  return upload_volume(guidev::g_device, cmd_, record->gpu, width, height, depth, pixels,
                       texel_bytes(format), resolved_target == GL_TEXTURE_3D);
}

/* -- WebGL 2 clearBuffer* ------------------------------------------------ */
void WebGLContext::clear_buffer_float(int buffer, int drawbuffer, const float *values,
                                      int count) {
  if (buffer == GL_COLOR) {
    if (drawbuffer != 0) return;
    clear_color(values[0], count > 1 ? values[1] : 0.0f, count > 2 ? values[2] : 0.0f,
                count > 3 ? values[3] : 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
    return;
  }
  if (buffer == GL_DEPTH) {
    clear_depth(values[0]);
    clear(GL_DEPTH_BUFFER_BIT);
    return;
  }
  if (buffer == GL_STENCIL) {
    clear_stencil((int)values[0]);
    clear(GL_STENCIL_BUFFER_BIT);
  }
}

void WebGLContext::clear_buffer_int(int buffer, int drawbuffer, const int *values, int count) {
  if (buffer == GL_STENCIL) {
    clear_stencil(values[0]);
    clear(GL_STENCIL_BUFFER_BIT);
    return;
  }
  if (buffer == GL_COLOR) {
    if (drawbuffer != 0) return;
    clear_color((float)values[0], count > 1 ? (float)values[1] : 0.0f,
                count > 2 ? (float)values[2] : 0.0f, count > 3 ? (float)values[3] : 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
  }
}

void WebGLContext::clear_buffer_uint(int buffer, int drawbuffer, const unsigned int *values,
                                     int count) {
  if (buffer == GL_STENCIL) {
    clear_stencil((int)values[0]);
    clear(GL_STENCIL_BUFFER_BIT);
    return;
  }
  if (buffer == GL_COLOR) {
    if (drawbuffer != 0) return;
    clear_color((float)values[0], count > 1 ? (float)values[1] : 0.0f,
                count > 2 ? (float)values[2] : 0.0f, count > 3 ? (float)values[3] : 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
  }
}

void WebGLContext::clear_buffer_fi(int buffer, int drawbuffer, float depth, int stencil) {
  (void)drawbuffer;
  if (buffer == GL_DEPTH_STENCIL) {
    clear_depth(depth);
    clear_stencil(stencil);
    clear(GL_DEPTH_BUFFER_BIT);
    return;
  }
  if (buffer == GL_DEPTH) {
    clear_depth(depth);
    clear(GL_DEPTH_BUFFER_BIT);
  } else if (buffer == GL_STENCIL) {
    clear_stencil(stencil);
    clear(GL_STENCIL_BUFFER_BIT);
  }
}

SDL_GPUTextureFormat webgl_storage_format(int internalformat) {
  if (internalformat == GL_DEPTH_COMPONENT16 || internalformat == GL_DEPTH_COMPONENT24 ||
      internalformat == GL_DEPTH_COMPONENT32F) {
    return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
  }
  if (internalformat == GL_DEPTH_STENCIL || internalformat == GL_DEPTH24_STENCIL8) {
    return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
  }
  if (internalformat == GL_RGBA32F || internalformat == GL_RGB32F ||
      internalformat == GL_RG32F || internalformat == GL_R32F) {
    return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
  }
  if (internalformat == GL_RGBA16F || internalformat == GL_RGB16F ||
      internalformat == GL_RG16F || internalformat == GL_R16F) {
    return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  }
  return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

}  // namespace xtgui
