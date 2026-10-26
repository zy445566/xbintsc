/*
 * xbintsc GUI engine — `blitFramebuffer`.
 *
 * SDL_GPU's blit copies a colour texture region to another colour texture, so
 * the colour bit maps directly. Depth/stencil blits are not supported by the
 * backend; those bits are ignored (the call is still accepted, like a driver
 * that cannot expand the blit). Sources must be single-sampled, which is what
 * this engine allocates today.
 */

#include "webgl.h"

#include <algorithm>
#include <cstring>

#include "gui_internal.h"

namespace xtgui {

bool WebGLContext::blit_framebuffer(const int source[4], const int destination[4], int mask,
                                    int filter) {
  if ((mask & GL_COLOR_BUFFER_BIT) == 0) return true;
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return false;
  SDL_GPUTexture *source_texture = framebuffer_color(read_framebuffer_);
  SDL_GPUTexture *destination_texture = framebuffer_color(bound_framebuffer_);
  if (source_texture == nullptr || destination_texture == nullptr ||
      source_texture == destination_texture) {
    return false;
  }
  int source_width = 0;
  int source_height = 0;
  int destination_width = 0;
  int destination_height = 0;
  framebuffer_size(read_framebuffer_, &source_width, &source_height);
  framebuffer_size(bound_framebuffer_, &destination_width, &destination_height);
  if (source_width <= 0 || source_height <= 0 || destination_width <= 0 ||
      destination_height <= 0) {
    return false;
  }
  int sx0 = source[0];
  int sy0 = source[1];
  int sx1 = source[2];
  int sy1 = source[3];
  int dx0 = destination[0];
  int dy0 = destination[1];
  int dx1 = destination[2];
  int dy1 = destination[3];
  if (sx1 < sx0) std::swap(sx0, sx1);
  if (sy1 < sy0) std::swap(sy0, sy1);
  if (dx1 < dx0) std::swap(dx0, dx1);
  if (dy1 < dy0) std::swap(dy0, dy1);
  sx0 = std::max(0, std::min(sx0, source_width));
  sx1 = std::max(0, std::min(sx1, source_width));
  sy0 = std::max(0, std::min(sy0, source_height));
  sy1 = std::max(0, std::min(sy1, source_height));
  dx0 = std::max(0, std::min(dx0, destination_width));
  dx1 = std::max(0, std::min(dx1, destination_width));
  dy0 = std::max(0, std::min(dy0, destination_height));
  dy1 = std::max(0, std::min(dy1, destination_height));
  if (sx1 <= sx0 || sy1 <= sy0 || dx1 <= dx0 || dy1 <= dy0) return true;

  SDL_GPUBlitRegion source_region;
  memset(&source_region, 0, sizeof(source_region));
  source_region.texture = source_texture;
  source_region.x = (Uint32)sx0;
  /* GL's origin is bottom-left; SDL's regions are top-down. */
  source_region.y = (Uint32)(source_height - sy1);
  source_region.w = (Uint32)(sx1 - sx0);
  source_region.h = (Uint32)(sy1 - sy0);

  SDL_GPUBlitRegion destination_region;
  memset(&destination_region, 0, sizeof(destination_region));
  destination_region.texture = destination_texture;
  destination_region.x = (Uint32)dx0;
  destination_region.y = (Uint32)(destination_height - dy1);
  destination_region.w = (Uint32)(dx1 - dx0);
  destination_region.h = (Uint32)(dy1 - dy0);

  SDL_GPUBlitInfo info;
  memset(&info, 0, sizeof(info));
  info.source = source_region;
  info.destination = destination_region;
  info.load_op = SDL_GPU_LOADOP_DONT_CARE;
  info.filter = filter == GL_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
  SDL_BlitGPUTexture(cmd_, &info);
  return true;
}

}  // namespace xtgui
