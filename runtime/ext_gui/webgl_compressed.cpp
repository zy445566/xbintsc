/*
 * xbintsc GUI engine — compressed textures (S3TC/BC and ASTC).
 *
 * SDL_GPU exposes block-compressed formats directly, so the engine only has to
 * map the WebGL extension enum to the SDL one and upload the tightly packed
 * blocks. Compressed textures are sample-only and get a single mip level.
 */

#include "webgl.h"

#include <cstring>

#include "gui_internal.h"

namespace xtgui {
namespace {

bool upload_compressed(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *gpu,
                       int x, int y, int width, int height, const uint8_t *data, uint32_t size) {
  if (gpu == nullptr || data == nullptr || size == 0 || width <= 0 || height <= 0) return false;
  SDL_GPUTransferBufferCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  info.size = size;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &info);
  if (transfer == nullptr) return false;
  void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  if (mapped == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return false;
  }
  memcpy(mapped, data, size);
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

SDL_GPUTextureFormat webgl_compressed_format(int internalformat) {
  switch (internalformat) {
    /* WEBGL_compressed_texture_s3tc */
    case 0x83F0:  // COMPRESSED_RGB_S3TC_DXT1_EXT
    case 0x83F1:  // COMPRESSED_RGBA_S3TC_DXT1_EXT
      return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case 0x83F2:  // COMPRESSED_RGBA_S3TC_DXT3_EXT
      return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
    case 0x83F3:  // COMPRESSED_RGBA_S3TC_DXT5_EXT
      return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    /* WEBGL_compressed_texture_s3tc_srgb */
    case 0x8C4C:  // COMPRESSED_SRGB_S3TC_DXT1_EXT
    case 0x8C4D:  // COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
      return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB;
    case 0x8C4E:  // COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT
      return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM_SRGB;
    case 0x8C4F:  // COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
      return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB;
    /* WEBGL_compressed_texture_astc (LDR) */
    case 0x93B0:
      return SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM;
    case 0x93B1:
      return SDL_GPU_TEXTUREFORMAT_ASTC_5x4_UNORM;
    case 0x93B2:
      return SDL_GPU_TEXTUREFORMAT_ASTC_5x5_UNORM;
    case 0x93B3:
      return SDL_GPU_TEXTUREFORMAT_ASTC_6x5_UNORM;
    case 0x93B4:
      return SDL_GPU_TEXTUREFORMAT_ASTC_6x6_UNORM;
    case 0x93B5:
      return SDL_GPU_TEXTUREFORMAT_ASTC_8x5_UNORM;
    case 0x93B6:
      return SDL_GPU_TEXTUREFORMAT_ASTC_8x6_UNORM;
    case 0x93B7:
      return SDL_GPU_TEXTUREFORMAT_ASTC_8x8_UNORM;
    case 0x93B8:
      return SDL_GPU_TEXTUREFORMAT_ASTC_10x5_UNORM;
    case 0x93B9:
      return SDL_GPU_TEXTUREFORMAT_ASTC_10x6_UNORM;
    case 0x93BA:
      return SDL_GPU_TEXTUREFORMAT_ASTC_10x8_UNORM;
    case 0x93BB:
      return SDL_GPU_TEXTUREFORMAT_ASTC_10x10_UNORM;
    case 0x93BC:
      return SDL_GPU_TEXTUREFORMAT_ASTC_12x10_UNORM;
    case 0x93BD:
      return SDL_GPU_TEXTUREFORMAT_ASTC_12x12_UNORM;
    /* ASTC sRGB */
    case 0x93D0:
      return SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM_SRGB;
    case 0x93D1:
      return SDL_GPU_TEXTUREFORMAT_ASTC_5x4_UNORM_SRGB;
    case 0x93D2:
      return SDL_GPU_TEXTUREFORMAT_ASTC_5x5_UNORM_SRGB;
    case 0x93D3:
      return SDL_GPU_TEXTUREFORMAT_ASTC_6x5_UNORM_SRGB;
    case 0x93D4:
      return SDL_GPU_TEXTUREFORMAT_ASTC_6x6_UNORM_SRGB;
    case 0x93D5:
      return SDL_GPU_TEXTUREFORMAT_ASTC_8x5_UNORM_SRGB;
    case 0x93D6:
      return SDL_GPU_TEXTUREFORMAT_ASTC_8x6_UNORM_SRGB;
    case 0x93D7:
      return SDL_GPU_TEXTUREFORMAT_ASTC_8x8_UNORM_SRGB;
    case 0x93D8:
      return SDL_GPU_TEXTUREFORMAT_ASTC_10x5_UNORM_SRGB;
    case 0x93D9:
      return SDL_GPU_TEXTUREFORMAT_ASTC_10x6_UNORM_SRGB;
    case 0x93DA:
      return SDL_GPU_TEXTUREFORMAT_ASTC_10x8_UNORM_SRGB;
    case 0x93DB:
      return SDL_GPU_TEXTUREFORMAT_ASTC_10x10_UNORM_SRGB;
    case 0x93DC:
      return SDL_GPU_TEXTUREFORMAT_ASTC_12x10_UNORM_SRGB;
    case 0x93DD:
      return SDL_GPU_TEXTUREFORMAT_ASTC_12x12_UNORM_SRGB;
    default:
      return SDL_GPU_TEXTUREFORMAT_INVALID;
  }
}

bool WebGLContext::supports_texture_format(int internalformat) {
  const SDL_GPUTextureFormat format = webgl_compressed_format(internalformat);
  if (format == SDL_GPU_TEXTUREFORMAT_INVALID || guidev::g_device == nullptr) return false;
  return SDL_GPUTextureSupportsFormat(guidev::g_device, format, SDL_GPU_TEXTURETYPE_2D,
                                      SDL_GPU_TEXTUREUSAGE_SAMPLER);
}

bool WebGLContext::tex_image_compressed(uint32_t id, int width, int height,
                                        SDL_GPUTextureFormat format, int x, int y,
                                        int region_width, int region_height,
                                        const uint8_t *data, uint32_t size) {
  WebGLTexture *record = texture(id);
  if (record == nullptr || width <= 0 || height <= 0 || format == SDL_GPU_TEXTUREFORMAT_INVALID) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return false;
  if (record->gpu != nullptr &&
      (record->width != width || record->height != height || record->format != format ||
       !record->compressed)) {
    SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, record->gpu);
    record->gpu = nullptr;
    record->usage = 0;
  }
  record->width = width;
  record->height = height;
  record->format = format;
  record->depth = false;
  record->compressed = true;
  record->has_mipmaps = false;
  if (record->sampler != nullptr) {
    SDL_ReleaseGPUSampler(guidev::g_device, record->sampler);
    record->sampler = nullptr;
  }
  if (ensure_texture_gpu(record, false) == nullptr) return false;
  record->defined = true;
  return upload_compressed(guidev::g_device, cmd_, record->gpu, x, y, region_width, region_height,
                           data, size);
}

}  // namespace xtgui
