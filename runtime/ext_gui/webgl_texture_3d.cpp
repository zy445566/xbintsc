/*
 * xbintsc GUI engine — 3D and 2D-array texture storage and uploads.
 *
 * SDL_GPU treats a 3D texture and a 2D-array as layered textures. Uploads are
 * performed one slice at a time because SDL's multi-slice transfer stride is
 * not usable for small 3D textures on the Metal backend.
 */

#include "webgl.h"

#include <cstring>
#include <vector>

#include "gui_internal.h"

namespace xtgui {
namespace {

int texel_bytes(SDL_GPUTextureFormat format) {
  if (format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) return 8;
  if (format == SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT) return 16;
  return 4;
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

/** Upload a sub-volume of a 3D/2D-array texture, one slice at a time. */
bool upload_sub_volume(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *gpu,
                       int x, int y, int z, int width, int height, int depth,
                       const uint8_t *pixels, int bytes_per_texel, bool is_3d) {
  const uint32_t slice_bytes = (uint32_t)(width * height * bytes_per_texel);
  const uint32_t bytes = slice_bytes * (uint32_t)depth;
  if (bytes == 0 || pixels == nullptr) return false;
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
    region.layer = is_3d ? 0 : (Uint32)(z + slice);
    region.z = is_3d ? (Uint32)(z + slice) : 0;
    region.x = (Uint32)x;
    region.y = (Uint32)y;
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

bool WebGLContext::tex_sub_image_3d(uint32_t id, int x, int y, int z, int width, int height,
                                      int depth, const uint8_t *pixels) {
  WebGLTexture *record = texture(id);
  if (record == nullptr || !record->defined || record->gpu == nullptr ||
      (record->target != GL_TEXTURE_3D && record->target != GL_TEXTURE_2D_ARRAY)) {
    set_error(GL_INVALID_OPERATION);
    return false;
  }
  if (x < 0 || y < 0 || z < 0 || width <= 0 || height <= 0 || depth <= 0 ||
      x + width > record->width || y + height > record->height ||
      z + depth > record->layers) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return false;
  return upload_sub_volume(guidev::g_device, cmd_, record->gpu, x, y, z, width, height, depth,
                           pixels, texel_bytes(record->format),
                           record->target == GL_TEXTURE_3D);
}

}  // namespace xtgui
