/*
 * xbintsc GUI engine — WebGL fixed-function state.
 *
 * SDL_GPU bakes blend/depth/stencil/rasterisation into a pipeline, so these
 * setters only record the values; `webgl_draw.cpp` hashes them into the
 * pipeline cache key and `webgl_pipeline.cpp` builds the matching state.
 */

#include "webgl.h"

#include <algorithm>
#include <cstring>

#include "gui_internal.h"

namespace xtgui {

/* -- state --------------------------------------------------------------- */

void WebGLContext::enable(int cap) {
  if (cap == GL_BLEND) state_.blend = true;
  if (cap == GL_DEPTH_TEST) state_.depth_test = true;
  if (cap == GL_CULL_FACE) state_.cull = true;
  if (cap == GL_SCISSOR_TEST) scissor_test_ = true;
  if (cap == GL_STENCIL_TEST) state_.stencil = true;
}

void WebGLContext::disable(int cap) {
  if (cap == GL_BLEND) state_.blend = false;
  if (cap == GL_DEPTH_TEST) state_.depth_test = false;
  if (cap == GL_CULL_FACE) state_.cull = false;
  if (cap == GL_SCISSOR_TEST) scissor_test_ = false;
  if (cap == GL_STENCIL_TEST) state_.stencil = false;
}

bool WebGLContext::is_enabled(int cap) const {
  if (cap == GL_BLEND) return state_.blend;
  if (cap == GL_DEPTH_TEST) return state_.depth_test;
  if (cap == GL_CULL_FACE) return state_.cull;
  if (cap == GL_SCISSOR_TEST) return scissor_test_;
  if (cap == GL_STENCIL_TEST) return state_.stencil;
  return false;
}

void WebGLContext::clear_color(float r, float g, float b, float a) {
  clear_color_[0] = r;
  clear_color_[1] = g;
  clear_color_[2] = b;
  clear_color_[3] = a;
}

void WebGLContext::color_mask(bool r, bool g, bool b, bool a) {
  state_.color_mask[0] = r;
  state_.color_mask[1] = g;
  state_.color_mask[2] = b;
  state_.color_mask[3] = a;
}

void WebGLContext::blend_func(int src, int dst) {
  state_.blend_src_rgb = src;
  state_.blend_dst_rgb = dst;
  state_.blend_src_alpha = src;
  state_.blend_dst_alpha = dst;
}

void WebGLContext::blend_func_separate(int src_rgb, int dst_rgb, int src_alpha, int dst_alpha) {
  state_.blend_src_rgb = src_rgb;
  state_.blend_dst_rgb = dst_rgb;
  state_.blend_src_alpha = src_alpha;
  state_.blend_dst_alpha = dst_alpha;
}

void WebGLContext::blend_equation(int mode) {
  state_.blend_eq_rgb = mode;
  state_.blend_eq_alpha = mode;
}

void WebGLContext::blend_equation_separate(int rgb, int alpha) {
  state_.blend_eq_rgb = rgb;
  state_.blend_eq_alpha = alpha;
}

void WebGLContext::blend_color(float r, float g, float b, float a) {
  blend_color_[0] = r;
  blend_color_[1] = g;
  blend_color_[2] = b;
  blend_color_[3] = a;
}

void WebGLContext::blend_color_value(float out[4]) const {
  for (int i = 0; i < 4; i++) out[i] = blend_color_[i];
}

void WebGLContext::cull_face(int mode) { state_.cull_face = mode; }

void WebGLContext::front_face(int mode) { state_.front_face = mode; }

void WebGLContext::stencil_func(int func, int ref, uint32_t mask) {
  stencil_func_separate(GL_FRONT_AND_BACK, func, ref, mask);
}

void WebGLContext::stencil_func_separate(int face, int func, int ref, uint32_t mask) {
  if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
    state_.stencil_front.func = func;
    state_.stencil_front.ref = ref;
    state_.stencil_front.value_mask = mask;
  }
  if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
    state_.stencil_back.func = func;
    state_.stencil_back.ref = ref;
    state_.stencil_back.value_mask = mask;
  }
}

void WebGLContext::stencil_op(int fail, int zfail, int zpass) {
  stencil_op_separate(GL_FRONT_AND_BACK, fail, zfail, zpass);
}

void WebGLContext::stencil_op_separate(int face, int fail, int zfail, int zpass) {
  if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
    state_.stencil_front.fail = fail;
    state_.stencil_front.zfail = zfail;
    state_.stencil_front.zpass = zpass;
  }
  if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
    state_.stencil_back.fail = fail;
    state_.stencil_back.zfail = zfail;
    state_.stencil_back.zpass = zpass;
  }
}

void WebGLContext::stencil_mask(uint32_t mask) {
  stencil_mask_separate(GL_FRONT_AND_BACK, mask);
}

void WebGLContext::stencil_mask_separate(int face, uint32_t mask) {
  if (face == GL_FRONT || face == GL_FRONT_AND_BACK) state_.stencil_front.write_mask = mask;
  if (face == GL_BACK || face == GL_FRONT_AND_BACK) state_.stencil_back.write_mask = mask;
}

const WebGLStencilFace &WebGLContext::stencil_face(int face) const {
  return face == GL_FRONT ? state_.stencil_front : state_.stencil_back;
}

int WebGLContext::stencil_reference() const {
  /* SDL_GPU has one stencil reference for a draw; the front value is the
   * WebGL default for both faces. */
  return state_.stencil_front.ref;
}

void WebGLContext::viewport(int x, int y, int w, int h) {
  viewport_[0] = x;
  viewport_[1] = y;
  viewport_[2] = w;
  viewport_[3] = h;
}

void WebGLContext::scissor(int x, int y, int w, int h) {
  scissor_box_[0] = x;
  scissor_box_[1] = y;
  scissor_box_[2] = w;
  scissor_box_[3] = h;
}

void WebGLContext::clear(int mask) {
  if (guidev::g_device == nullptr) return;
  const bool color_bit = (mask & GL_COLOR_BUFFER_BIT) != 0;
  const bool depth_bit = (mask & GL_DEPTH_BUFFER_BIT) != 0;
  const bool stencil_bit = (mask & GL_STENCIL_BUFFER_BIT) != 0;
  const bool scissored = scissor_test_ && scissor_box_[2] > 0 && scissor_box_[3] > 0;
  if (!scissored) {
    begin_pass(color_bit, depth_bit, stencil_bit);
    return;
  }
  if (color_bit) {
    SDL_GPUTexture *source = clear_color_texture();
    SDL_GPUTexture *destination = target_color();
    int target_width = 0;
    int target_height = 0;
    framebuffer_size(bound_framebuffer_, &target_width, &target_height);
    if (source != nullptr && destination != nullptr && target_width > 0 && target_height > 0) {
      end_pass();
      ensure_command();
      if (cmd_ != nullptr) {
        int x = std::max(0, scissor_box_[0]);
        int y = std::max(0, scissor_box_[1]);
        int w = scissor_box_[2];
        int h = scissor_box_[3];
        if (x + w > target_width) w = target_width - x;
        if (y + h > target_height) h = target_height - y;
        if (w > 0 && h > 0) {
          SDL_GPUBlitRegion source_region;
          memset(&source_region, 0, sizeof(source_region));
          source_region.texture = source;
          source_region.w = 1;
          source_region.h = 1;
                  SDL_GPUBlitRegion destination_region;
          memset(&destination_region, 0, sizeof(destination_region));
          destination_region.texture = destination;
          destination_region.x = (Uint32)x;
          destination_region.y = (Uint32)(target_height - (y + h));
          destination_region.w = (Uint32)w;
          destination_region.h = (Uint32)h;
                  SDL_GPUBlitInfo info;
          memset(&info, 0, sizeof(info));
          info.source = source_region;
          info.destination = destination_region;
          info.load_op = SDL_GPU_LOADOP_DONT_CARE;
          info.filter = SDL_GPU_FILTER_NEAREST;
          SDL_BlitGPUTexture(cmd_, &info);
        }
      }
    }
  }
  if (depth_bit || stencil_bit) scissored_clear(mask);
}

void WebGLContext::finish() {
  submit();
  if (guidev::g_device != nullptr) SDL_WaitForGPUIdle(guidev::g_device);
}

}  // namespace xtgui
