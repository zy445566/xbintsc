/*
 * xbintsc GUI engine — WebGL texture uploads, mipmaps and `readPixels`.
 */

#include "webgl.h"

#include <algorithm>
#include <cstring>

#include "gui_internal.h"

namespace xtgui {
namespace {

/** Upload an RGBA8 rectangle into level 0 of `gpu`. `rgba` may be null, which
 * uploads transparent black. */
bool upload_region(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *gpu, int x,
                   int y, int width, int height, const uint8_t *rgba) {
  const uint32_t bytes = (uint32_t)(width * height * 4);
  if (bytes == 0) return true;
  std::vector<uint8_t> zeros;
  if (rgba == nullptr) {
    zeros.assign(bytes, 0);
    rgba = zeros.data();
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
  memcpy(mapped, rgba, bytes);
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
  region.w = (Uint32)width;
  region.h = (Uint32)height;
  region.d = 1;
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_UploadToGPUTexture(copy, &source, &region, false);
  SDL_EndGPUCopyPass(copy);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  return true;
}

}  // namespace

bool WebGLContext::tex_image_rgba(uint32_t id, int width, int height, const uint8_t *rgba) {
  WebGLTexture *record = texture(id);
  if (record == nullptr || width <= 0 || height <= 0) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return false;
  if (record->width != width || record->height != height) {
    if (record->gpu != nullptr) {
      SDL_WaitForGPUIdle(guidev::g_device);
      SDL_ReleaseGPUTexture(guidev::g_device, record->gpu);
      record->gpu = nullptr;
    }
    record->width = width;
    record->height = height;
    record->has_mipmaps = false;
    if (record->sampler != nullptr) {
      SDL_ReleaseGPUSampler(guidev::g_device, record->sampler);
      record->sampler = nullptr;
    }
  }
  if (ensure_texture_gpu(record, record->as_target) == nullptr) return false;
  record->defined = true;
  return upload_region(guidev::g_device, cmd_, record->gpu, 0, 0, width, height, rgba);
}

bool WebGLContext::tex_sub_image_rgba(uint32_t id, int x, int y, int width, int height,
                                      const uint8_t *rgba) {
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
  return upload_region(guidev::g_device, cmd_, record->gpu, x, y, width, height, rgba);
}

bool WebGLContext::generate_mipmap(uint32_t id) {
  WebGLTexture *record = texture(id);
  if (record == nullptr || record->gpu == nullptr) {
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
  SDL_GPUTexture *target = target_color();
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

}  // namespace xtgui
