/*
 * xbintsc GUI engine — the fixed-function WebGL state a pipeline depends on.
 *
 * Split out of `webgl.h` to keep that header under the repository's
 * file-length budget.
 */
#ifndef XT_GUI_WEBGL_DRAW_STATE_H
#define XT_GUI_WEBGL_DRAW_STATE_H

#include <cstdint>

#include <SDL3/SDL.h>

#include "webgl_constants.h"

namespace xtgui {

/** One face's stencil test and update state. */
struct WebGLStencilFace {
  int func = GL_ALWAYS;
  int ref = 0;
  uint32_t value_mask = 0xFFFFFFFFu;  // compare mask
  uint32_t write_mask = 0xFFFFFFFFu;
  int fail = GL_KEEP;
  int zfail = GL_KEEP;
  int zpass = GL_KEEP;
};

/** The fixed-function state a pipeline depends on. */
struct WebGLDrawState {
  bool blend = false;
  int blend_src_rgb = GL_ONE;
  int blend_dst_rgb = GL_ZERO;
  int blend_src_alpha = GL_ONE;
  int blend_dst_alpha = GL_ZERO;
  int blend_eq_rgb = GL_FUNC_ADD;
  int blend_eq_alpha = GL_FUNC_ADD;
  bool depth_test = false;
  bool depth_write = true;
  int depth_func = GL_LESS;
  bool stencil = false;
  WebGLStencilFace stencil_front;
  WebGLStencilFace stencil_back;
  bool cull = false;
  int cull_face = GL_BACK;
  int front_face = GL_CCW;
  bool color_mask[4] = {true, true, true, true};
  /** Attachment formats of the bound target; pipelines are format-specific. */
  int color_formats[4] = {
      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};
  int color_target_count = 1;
  int depth_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
};

}  // namespace xtgui

#endif /* XT_GUI_WEBGL_DRAW_STATE_H */
