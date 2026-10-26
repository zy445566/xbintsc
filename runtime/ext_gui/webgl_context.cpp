/*
 * xbintsc GUI engine — WebGL context lifetime, GPU targets and fixed-function
 * state.
 *
 * Every context records into one SDL_GPU command buffer. A render pass stays
 * open across draws and is ended whenever a copy pass (buffer/texture upload)
 * or a different attachment has to be recorded; the next draw starts a new
 * pass with `LOAD`, so the framebuffer persists like a WebGL drawing buffer.
 */

#include "webgl.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "gui_internal.h"

namespace xtgui {
namespace {

std::unordered_map<uint64_t, WebGLContext *> g_contexts;

}  // namespace

WebGLContext *xt_webgl_by_id(uint64_t id) {
  auto found = g_contexts.find(id);
  return found == g_contexts.end() ? nullptr : found->second;
}

WebGLContext::WebGLContext(uint64_t id, int width, int height, bool webgl2)
    : id_(id), webgl2_(webgl2) {
  width_ = width > 0 ? width : 1;
  height_ = height > 0 ? height : 1;
  viewport_[0] = 0;
  viewport_[1] = 0;
  viewport_[2] = width_;
  viewport_[3] = height_;
  scissor_box_[0] = 0;
  scissor_box_[1] = 0;
  scissor_box_[2] = width_;
  scissor_box_[3] = height_;
  g_contexts[id] = this;
  if (guidev::g_device == nullptr) lost_ = true;
}

WebGLContext::~WebGLContext() {
  submit();
  if (guidev::g_device != nullptr) {
    SDL_WaitForGPUIdle(guidev::g_device);
    for (auto &entry : programs_) release_program_pipelines(entry.second.get());
    for (auto &entry : textures_) {
      if (entry.second->gpu != nullptr) SDL_ReleaseGPUTexture(guidev::g_device, entry.second->gpu);
      if (entry.second->sampler != nullptr) {
        SDL_ReleaseGPUSampler(guidev::g_device, entry.second->sampler);
      }
    }
    for (auto &entry : buffers_) {
      if (entry.second->gpu != nullptr) SDL_ReleaseGPUBuffer(guidev::g_device, entry.second->gpu);
    }
    for (auto &entry : samplers_) {
      if (entry.second->gpu != nullptr) {
        SDL_ReleaseGPUSampler(guidev::g_device, entry.second->gpu);
      }
    }
    for (auto &entry : framebuffers_) {
      if (entry.second->depth != nullptr) SDL_ReleaseGPUTexture(guidev::g_device, entry.second->depth);
    }
    for (auto &entry : renderbuffers_) {
      if (entry.second->depth != nullptr) SDL_ReleaseGPUTexture(guidev::g_device, entry.second->depth);
    }
    for (SDL_GPUBuffer *buffer : temp_buffers_) {
      SDL_ReleaseGPUBuffer(guidev::g_device, buffer);
    }
    if (fallback_texture_ != nullptr) SDL_ReleaseGPUTexture(guidev::g_device, fallback_texture_);
    if (fallback_sampler_ != nullptr) SDL_ReleaseGPUSampler(guidev::g_device, fallback_sampler_);
    if (color_ != nullptr) SDL_ReleaseGPUTexture(guidev::g_device, color_);
    if (depth_ != nullptr) SDL_ReleaseGPUTexture(guidev::g_device, depth_);
  }
  g_contexts.erase(id_);
}

/* -- targets ------------------------------------------------------------- */

void WebGLContext::ensure_command() {
  if (cmd_ != nullptr || guidev::g_device == nullptr) return;
  cmd_ = SDL_AcquireGPUCommandBuffer(guidev::g_device);
  if (cmd_ == nullptr) {
    lost_ = true;
    set_error(GL_OUT_OF_MEMORY);
  }
}

void WebGLContext::ensure_default_target() {
  if (guidev::g_device == nullptr) return;
  if (color_ != nullptr && depth_ != nullptr) return;
  SDL_GPUTextureCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  info.width = (Uint32)width_;
  info.height = (Uint32)height_;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  if (color_ == nullptr) color_ = SDL_CreateGPUTexture(guidev::g_device, &info);
  info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
  info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
  if (depth_ == nullptr) depth_ = SDL_CreateGPUTexture(guidev::g_device, &info);
}

void WebGLContext::rebuild_default_target() {
  if (guidev::g_device == nullptr) return;
  end_pass();
  ensure_command();
  if (cmd_ != nullptr) {
    /* Nothing recorded into the old target is worth keeping: a resize discards
     * the drawing buffer, exactly like the DOM `canvas.width = …` rule. */
    SDL_SubmitGPUCommandBuffer(cmd_);
    cmd_ = nullptr;
  }
  SDL_WaitForGPUIdle(guidev::g_device);
  if (color_ != nullptr) SDL_ReleaseGPUTexture(guidev::g_device, color_);
  if (depth_ != nullptr) SDL_ReleaseGPUTexture(guidev::g_device, depth_);
  color_ = nullptr;
  depth_ = nullptr;
  ensure_default_target();
}

SDL_GPUTexture *WebGLContext::color_texture() {
  ensure_default_target();
  return color_;
}

void WebGLContext::resize(int width, int height) {
  if (width < 0) width = 0;
  if (height < 0) height = 0;
  width_ = width > 0 ? width : 1;
  height_ = height > 0 ? height : 1;
  viewport_[0] = 0;
  viewport_[1] = 0;
  viewport_[2] = width_;
  viewport_[3] = height_;
  scissor_box_[0] = 0;
  scissor_box_[1] = 0;
  scissor_box_[2] = width_;
  scissor_box_[3] = height_;
  rebuild_default_target();
}

void WebGLContext::submit() {
  end_pass();
  if (cmd_ != nullptr) {
    SDL_SubmitGPUCommandBuffer(cmd_);
    cmd_ = nullptr;
  }
}

/* -- passes -------------------------------------------------------------- */

SDL_GPUTexture *WebGLContext::target_color() {
  if (bound_framebuffer_ == 0) {
    ensure_default_target();
    return color_;
  }
  WebGLFramebuffer *framebuffer = this->framebuffer(bound_framebuffer_);
  if (framebuffer == nullptr || framebuffer->color_texture == 0) return nullptr;
  WebGLTexture *texture = this->texture(framebuffer->color_texture);
  if (texture == nullptr) return nullptr;
  return ensure_texture_gpu(texture, true);
}

SDL_GPUTexture *WebGLContext::target_depth() {
  if (bound_framebuffer_ == 0) {
    ensure_default_target();
    return depth_;
  }
  WebGLFramebuffer *framebuffer = this->framebuffer(bound_framebuffer_);
  if (framebuffer == nullptr) return nullptr;
  if (framebuffer->depth == nullptr && framebuffer->depth_renderbuffer == 0 &&
      framebuffer->width > 0 && framebuffer->height > 0 && guidev::g_device != nullptr) {
    SDL_GPUTextureCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    info.width = (Uint32)framebuffer->width;
    info.height = (Uint32)framebuffer->height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    framebuffer->depth = SDL_CreateGPUTexture(guidev::g_device, &info);
  }
  return framebuffer->depth;
}

void WebGLContext::end_pass() {
  if (pass_ != nullptr && cmd_ != nullptr) {
    SDL_EndGPURenderPass(pass_);
    pass_ = nullptr;
  }
}

void WebGLContext::begin_pass(bool clear_color, bool clear_depth) {
  if (guidev::g_device == nullptr) return;
  if (pass_ != nullptr && pass_targets_default_ == (bound_framebuffer_ == 0) &&
      pass_framebuffer_ == bound_framebuffer_ && !clear_color && !clear_depth) {
    return;
  }
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return;
  SDL_GPUTexture *color = target_color();
  SDL_GPUTexture *depth = target_depth();
  if (color == nullptr) {
    set_error(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  SDL_GPUColorTargetInfo color_info;
  memset(&color_info, 0, sizeof(color_info));
  color_info.texture = color;
  color_info.clear_color.r = clear_color_[0];
  color_info.clear_color.g = clear_color_[1];
  color_info.clear_color.b = clear_color_[2];
  color_info.clear_color.a = clear_color_[3];
  color_info.load_op = clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
  color_info.store_op = SDL_GPU_STOREOP_STORE;

  SDL_GPUDepthStencilTargetInfo depth_info;
  memset(&depth_info, 0, sizeof(depth_info));
  depth_info.texture = depth;
  depth_info.clear_depth = clear_depth_;
  depth_info.load_op = (clear_depth && depth != nullptr) ? SDL_GPU_LOADOP_CLEAR
                                                         : SDL_GPU_LOADOP_LOAD;
  depth_info.store_op = SDL_GPU_STOREOP_STORE;
  depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

  pass_ = SDL_BeginGPURenderPass(cmd_, &color_info, 1, depth != nullptr ? &depth_info : nullptr);
  pass_targets_default_ = bound_framebuffer_ == 0;
  pass_framebuffer_ = bound_framebuffer_;
}

/* -- state --------------------------------------------------------------- */

void WebGLContext::enable(int cap) {
  if (cap == GL_BLEND) state_.blend = true;
  if (cap == GL_DEPTH_TEST) state_.depth_test = true;
  if (cap == GL_CULL_FACE) state_.cull = true;
  if (cap == GL_SCISSOR_TEST) scissor_test_ = true;
  if (cap == GL_STENCIL_TEST) stencil_test_ = true;
}

void WebGLContext::disable(int cap) {
  if (cap == GL_BLEND) state_.blend = false;
  if (cap == GL_DEPTH_TEST) state_.depth_test = false;
  if (cap == GL_CULL_FACE) state_.cull = false;
  if (cap == GL_SCISSOR_TEST) scissor_test_ = false;
  if (cap == GL_STENCIL_TEST) stencil_test_ = false;
}

bool WebGLContext::is_enabled(int cap) const {
  if (cap == GL_BLEND) return state_.blend;
  if (cap == GL_DEPTH_TEST) return state_.depth_test;
  if (cap == GL_CULL_FACE) return state_.cull;
  if (cap == GL_SCISSOR_TEST) return scissor_test_;
  if (cap == GL_STENCIL_TEST) return stencil_test_;
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

void WebGLContext::cull_face(int mode) { state_.cull_face = mode; }

void WebGLContext::front_face(int mode) { state_.front_face = mode; }

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
  /* The scissor rectangle is not honoured by a pass-begin clear; WebGL pages
   * that clear a sub-rectangle are rare and documented as a limitation. */
  begin_pass(color_bit, depth_bit);
}

void WebGLContext::finish() {
  submit();
  if (guidev::g_device != nullptr) SDL_WaitForGPUIdle(guidev::g_device);
}

/* -- queries ------------------------------------------------------------- */

double WebGLContext::parameter(int pname) {
  switch (pname) {
    case GL_MAX_TEXTURE_SIZE:
      return 8192;
    case GL_MAX_VERTEX_ATTRIBS:
      return kWebGLMaxAttribs;
    case GL_MAX_TEXTURE_IMAGE_UNITS:
      return kWebGLMaxTextureUnits;
    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
      return kWebGLMaxTextureUnits * 2;
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
      return 0;
    case GL_MAX_VERTEX_UNIFORM_VECTORS:
    case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
    case GL_MAX_VARYING_VECTORS:
      return 256;
    case GL_MAX_RENDERBUFFER_SIZE:
      return 8192;
    case GL_MAX_VIEWPORT_DIMS:
      return 8192;
    case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
      return 0;
    case GL_SAMPLES:
    case GL_SAMPLE_BUFFERS:
      return 0;
    case GL_SUBPIXEL_BITS:
      return 4;
    case GL_RED_BITS:
    case GL_GREEN_BITS:
    case GL_BLUE_BITS:
    case GL_ALPHA_BITS:
    case GL_DEPTH_BITS:
      return 8;
    case GL_STENCIL_BITS:
      return 0;
    case GL_UNPACK_ALIGNMENT:
    case GL_PACK_ALIGNMENT:
      return 4;
    case GL_ACTIVE_TEXTURE:
      return active_unit_ + GL_TEXTURE0;
    case GL_CURRENT_PROGRAM:
      return current_program_;
    case GL_DEPTH_CLEAR_VALUE:
      return clear_depth_;
    case GL_STENCIL_CLEAR_VALUE:
      return clear_stencil_;
    /* -- WebGL 2 --------------------------------------------------------- */
    case GL_MAX_3D_TEXTURE_SIZE:
      return 2048;
    case GL_MAX_ARRAY_TEXTURE_LAYERS:
      return 256;
    case GL_MAX_DRAW_BUFFERS:
    case GL_MAX_COLOR_ATTACHMENTS:
      return 1;
    case GL_MAX_SAMPLES:
      return 0;
    case GL_MAX_UNIFORM_BUFFER_BINDINGS:
    case GL_MAX_VERTEX_UNIFORM_BLOCKS:
    case GL_MAX_FRAGMENT_UNIFORM_BLOCKS:
      return 0;
    case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
      return 256;
    case GL_MAX_ELEMENT_INDEX:
      return 4294967295.0;
    case GL_MAX_CLIENT_WAIT_TIMEOUT_WEBGL:
      return 0;
    case GL_MAX_VARYING_COMPONENTS:
    case GL_MAX_FRAGMENT_INPUT_COMPONENTS:
      return 60;
    case GL_MAX_VERTEX_OUTPUT_COMPONENTS:
      return 64;
    case GL_MAX_VERTEX_UNIFORM_COMPONENTS:
    case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS:
    case GL_MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS:
    case GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS:
      return 1024;
    case GL_MAX_UNIFORM_BLOCK_SIZE:
      return 16384;
    case GL_READ_BUFFER:
    case GL_DRAW_BUFFER0:
      return GL_BACK;
    default:
      return 0;
  }
}

std::string WebGLContext::string_parameter(int pname) {
  const char *driver_name = driver();
  switch (pname) {
    case GL_VENDOR:
      return "xbintsc";
    case GL_RENDERER:
      return std::string("xbintsc gui (SDL_GPU ") + (driver_name != nullptr ? driver_name : "?") +
             ")";
    case GL_VERSION:
      return webgl2_ ? "WebGL 2.0 (xbintsc)" : "WebGL 1.0 (xbintsc)";
    case GL_SHADING_LANGUAGE_VERSION:
      return webgl2_ ? "WebGL GLSL ES 3.00 (xbintsc)" : "WebGL GLSL ES 1.0 (xbintsc)";
    default:
      return std::string();
  }
}

const char *WebGLContext::driver() const {
  return xt_gui_driver();
}

uint32_t WebGLContext::bound_buffer(int target) const { return buffer_for_target(target); }

uint32_t WebGLContext::bound_texture(int unit) const {
  if (unit < 0 || unit >= kWebGLMaxTextureUnits) return 0;
  return bound_textures_[unit];
}

void WebGLContext::viewport_box(int out[4]) const {
  for (int i = 0; i < 4; i++) out[i] = viewport_[i];
}

void WebGLContext::scissor_box(int out[4]) const {
  for (int i = 0; i < 4; i++) out[i] = scissor_box_[i];
}

}  // namespace xtgui
