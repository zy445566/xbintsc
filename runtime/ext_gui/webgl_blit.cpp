/*
 * xbintsc GUI engine — `blitFramebuffer`.
 *
 * SDL_GPU's blit copies a single-sampled colour texture region to another
 * single-sampled colour texture, so the colour bit maps directly. A
 * multisampled source is first resolved into an internal single-sampled
 * texture (the drawing buffer is already resolved, so the default framebuffer
 * needs no extra pass); a multisampled *destination* is an error, as
 * OpenGL ES 3.0 requires. Depth/stencil blits are not supported by the
 * backend; those bits are ignored (the call is still accepted, like a driver
 * that cannot expand the blit).
 */

#include "webgl.h"

#include <algorithm>
#include <cstring>

#include "gui_internal.h"

namespace xtgui {
namespace {

bool trace_enabled() {
  static int value = -1;
  if (value < 0) {
    const char *env = SDL_getenv("XT_GUI_TRACE");
    value = (env != nullptr && env[0] != '\0' && env[0] != '0') ? 1 : 0;
  }
  return value != 0;
}

}  // namespace

bool WebGLContext::blit_framebuffer(const int source[4], const int destination[4], int mask,
                                    int filter) {
  const bool color_bit = (mask & GL_COLOR_BUFFER_BIT) != 0;
  const bool depth_bit = (mask & GL_DEPTH_BUFFER_BIT) != 0;
  if (!color_bit && !depth_bit) return true;
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return false;

  const int source_samples = framebuffer_samples(read_framebuffer_);
  const int destination_samples = framebuffer_samples(bound_framebuffer_);
  if (trace_enabled()) {
    fprintf(stderr,
            "xt_gui: blit read=%u draw=%u mask=%d samples=%d->%d rects=%d,%d,%d,%d -> %d,%d,%d,%d\n",
            read_framebuffer_, bound_framebuffer_, mask, source_samples, destination_samples,
            source[0], source[1], source[2], source[3], destination[0], destination[1],
            destination[2], destination[3]);
  }
  if (destination_samples > 1) {
    /* ES 3.0 §4.3.2: an INVALID_OPERATION is generated when the draw
     * framebuffer is multisampled, so a page renders into its own resolve
     * target instead of the antialiased canvas. */
    set_error(GL_INVALID_OPERATION);
    return false;
  }
  if (depth_bit) {
    /* SDL_GPU's blit API is colour-only; the depth copy is a sampling pass that
     * writes `gl_FragDepth`. Stencil bits have no sampleable representation and
     * are left untouched. */
    blit_depth(read_framebuffer_, bound_framebuffer_, source, destination);
    end_pass();
    ensure_command();
  }
  if (!color_bit) return true;

  int source_width = 0;
  int source_height = 0;
  int source_format = 0;
  int destination_width = 0;
  int destination_height = 0;
  int destination_format = 0;
  int destination_samples_unused = 1;
  SDL_GPUTexture *source_texture = read_color_texture(read_framebuffer_, &source_width,
                                                      &source_height, &source_format);
  SDL_GPUTexture *destination_texture =
      color_attachment(bound_framebuffer_, 0, &destination_format, &destination_samples_unused,
                       &destination_width, &destination_height);
  if (source_texture == nullptr || destination_texture == nullptr ||
      source_texture == destination_texture) {
    return false;
  }
  if (source_width <= 0 || source_height <= 0 || destination_width <= 0 ||
      destination_height <= 0) {
    return false;
  }
  if (source_samples > 1) {
    /* Resolving is only defined for identical rectangles and identical colour
     * formats; with a different rectangle or format the ES 3.0 spec raises
     * INVALID_OPERATION instead of filtering the samples. */
    if (source[0] != destination[0] || source[1] != destination[1] ||
        source[2] != destination[2] || source[3] != destination[3]) {
      set_error(GL_INVALID_OPERATION);
      return false;
    }
    if (source_format != destination_format || filter != GL_NEAREST) {
      set_error(GL_INVALID_OPERATION);
      return false;
    }
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

  /* `SDL_BlitGPUTexture` is implemented with `vkCmdBlitImage`, which requires
   * the source and destination formats to advertise BLIT_SRC/BLIT_DST. The
   * half-float formats a three.js scene render target uses do not, and the
   * invalid command loses the Vulkan device. Copy the resolved image with a
   * sampling pass instead; a full-rectangle resolve is the common case (the
   * ES 3.0 resolve path already requires identical source/destination
   * rectangles). */
  const bool full_rect = sx0 == 0 && sy0 == 0 && sx1 == source_width && sy1 == source_height &&
                         dx0 == 0 && dy0 == 0 && dx1 == destination_width &&
                         dy1 == destination_height;
  const bool float_format =
      source_format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT ||
      source_format == SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT ||
      source_format == SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT ||
      source_format == SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT ||
      source_format == SDL_GPU_TEXTUREFORMAT_R16_FLOAT ||
      source_format == SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
  if (full_rect && float_format && source_samples > 1) {
    if (draw_color_quad(source_texture, source_width, source_height, destination)) return true;
  }

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
  /* A blit only writes the destination rectangle; everything outside it keeps
   * its previous contents, as `blitFramebuffer` requires. DONT_CARE lets a
   * backend (Metal's MTLLoadActionDontCare) discard the attachment and leave
   * those pixels undefined, so load it. */
  info.load_op = SDL_GPU_LOADOP_LOAD;
  info.filter = filter == GL_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
  SDL_ClearError();
  SDL_BlitGPUTexture(cmd_, &info);
  if (trace_enabled()) {
    const char *error = SDL_GetError();
    if (error != nullptr && error[0] != '\0') {
      fprintf(stderr, "xt_gui: blit failed (%d -> %d, format %d -> %d): %s\n", source_samples,
              destination_samples, source_format, destination_format, error);
    }
  }
  return true;
}

}  // namespace xtgui
