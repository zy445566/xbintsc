/*
 * xbintsc GUI engine — WebGL context lifetime and GPU targets.
 *
 * Every context records into one SDL_GPU command buffer. A render pass stays
 * open across draws and is ended whenever a copy pass (buffer/texture upload)
 * or a different attachment has to be recorded; the next draw starts a new
 * pass with `LOAD`, so the framebuffer persists like a WebGL drawing buffer.
 * Fixed-function state lives in `webgl_state.cpp`.
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
  if (guidev::g_device != nullptr) release_gpu_resources();
  g_contexts.erase(id_);
}

void WebGLContext::release_gpu_resources() {
  if (guidev::g_device == nullptr) return;
  SDL_WaitForGPUIdle(guidev::g_device);
  for (auto &entry : programs_) release_program_pipelines(entry.second.get());
  for (auto &entry : textures_) {
    if (entry.second->gpu != nullptr) SDL_ReleaseGPUTexture(guidev::g_device, entry.second->gpu);
    if (entry.second->sampler != nullptr) {
      SDL_ReleaseGPUSampler(guidev::g_device, entry.second->sampler);
    }
    if (entry.second->compare_sampler != nullptr) {
      SDL_ReleaseGPUSampler(guidev::g_device, entry.second->compare_sampler);
    }
    entry.second->gpu = nullptr;
    entry.second->sampler = nullptr;
    entry.second->compare_sampler = nullptr;
  }
  for (auto &entry : buffers_) {
    if (entry.second->gpu != nullptr) SDL_ReleaseGPUBuffer(guidev::g_device, entry.second->gpu);
    entry.second->gpu = nullptr;
    entry.second->capacity = 0;
  }
  for (auto &entry : samplers_) {
    if (entry.second->gpu != nullptr) {
      SDL_ReleaseGPUSampler(guidev::g_device, entry.second->gpu);
      entry.second->gpu = nullptr;
    }
  }
  for (auto &entry : framebuffers_) {
    if (entry.second->depth != nullptr) {
      SDL_ReleaseGPUTexture(guidev::g_device, entry.second->depth);
      entry.second->depth = nullptr;
    }
  }
  for (auto &entry : renderbuffers_) {
    if (entry.second->depth != nullptr) {
      SDL_ReleaseGPUTexture(guidev::g_device, entry.second->depth);
      entry.second->depth = nullptr;
    }
  }
  for (SDL_GPUBuffer *buffer : temp_buffers_) SDL_ReleaseGPUBuffer(guidev::g_device, buffer);
  temp_buffers_.clear();
  if (fallback_texture_ != nullptr) {
    SDL_ReleaseGPUTexture(guidev::g_device, fallback_texture_);
    fallback_texture_ = nullptr;
  }
  if (fallback_sampler_ != nullptr) {
    SDL_ReleaseGPUSampler(guidev::g_device, fallback_sampler_);
    fallback_sampler_ = nullptr;
  }
  if (blit_sampler_ != nullptr) {
    SDL_ReleaseGPUSampler(guidev::g_device, blit_sampler_);
    blit_sampler_ = nullptr;
  }
  if (color_ != nullptr) {
    SDL_ReleaseGPUTexture(guidev::g_device, color_);
    color_ = nullptr;
  }
  if (depth_ != nullptr) {
    SDL_ReleaseGPUTexture(guidev::g_device, depth_);
    depth_ = nullptr;
  }
  if (clear_texture_ != nullptr) {
    SDL_ReleaseGPUTexture(guidev::g_device, clear_texture_);
    clear_texture_ = nullptr;
  }
}

void WebGLContext::lose_context() {
  if (lost_) return;
  submit();
  if (guidev::g_device != nullptr) release_gpu_resources();
  /* The WebGL spec invalidates every object when the context is lost: a page
   * that gets `webglcontextlost` must recreate buffers, textures and programs
   * after the restore. */
  shaders_.clear();
  programs_.clear();
  buffers_.clear();
  textures_.clear();
  framebuffers_.clear();
  renderbuffers_.clear();
  vertex_arrays_.clear();
  samplers_.clear();
  queries_.clear();
  syncs_.clear();
  default_vertex_array_ = WebGLVertexArray();
  for (WebGLAttribState &attrib : attribs_) attrib = WebGLAttribState();
  current_program_ = 0;
  bound_array_ = 0;
  bound_element_ = 0;
  bound_framebuffer_ = 0;
  read_framebuffer_ = 0;
  bound_renderbuffer_ = 0;
  bound_vertex_array_ = 0;
  copy_read_buffer_ = 0;
  copy_write_buffer_ = 0;
  active_unit_ = 0;
  for (uint32_t &texture : bound_textures_) texture = 0;
  for (uint32_t &texture : bound_cube_textures_) texture = 0;
  for (uint32_t &texture : bound_array_textures_) texture = 0;
  for (uint32_t &texture : bound_3d_textures_) texture = 0;
  for (int &target : active_texture_target_) target = GL_TEXTURE_2D;
  for (uint32_t &sampler : bound_samplers_) sampler = 0;
  state_ = WebGLDrawState();
  scissor_test_ = false;
  error_ = 0;
  lost_ = true;
  clear_program_ = 0;
  clear_buffer_ = 0;
}

void WebGLContext::restore_context() {
  if (!lost_) return;
  if (guidev::g_device == nullptr) return;
  lost_ = false;
  error_ = 0;
  ensure_default_target();
}

void WebGLContext::drawing_buffer_storage(int width, int height) {
  if (width <= 0 || height <= 0) {
    set_error(GL_INVALID_VALUE);
    return;
  }
  fixed_storage_ = true;
  if (width == width_ && height == height_) return;
  width_ = width;
  height_ = height;
  rebuild_default_target();
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
  info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
  /* SAMPLER too: `blitFramebuffer` may copy the default depth attachment. */
  info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
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
  if (lost_) return nullptr;
  ensure_default_target();
  return color_;
}

void WebGLContext::resize(int width, int height) {
  /* After `drawingBufferStorage` the drawing buffer has an explicit size and a
   * canvas resize no longer changes it (WebGL 2). */
  if (fixed_storage_) return;
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
  if (framebuffer == nullptr || framebuffer->color_textures[0] == 0) return nullptr;
  WebGLTexture *texture = this->texture(framebuffer->color_textures[0]);
  if (texture == nullptr) return nullptr;
  return ensure_texture_gpu(texture, true);
}

SDL_GPUTexture *WebGLContext::framebuffer_color(uint32_t id) {
  if (id == 0) {
    ensure_default_target();
    return color_;
  }
  WebGLFramebuffer *framebuffer_ = framebuffer(id);
  if (framebuffer_ == nullptr || framebuffer_->color_textures[0] == 0) return nullptr;
  WebGLTexture *texture_ = texture(framebuffer_->color_textures[0]);
  return texture_ != nullptr ? ensure_texture_gpu(texture_, true) : nullptr;
}

void WebGLContext::framebuffer_size(uint32_t id, int *width, int *height) {
  if (id == 0) {
    *width = width_;
    *height = height_;
    return;
  }
  WebGLFramebuffer *framebuffer_ = framebuffer(id);
  *width = framebuffer_ != nullptr ? framebuffer_->width : 0;
  *height = framebuffer_ != nullptr ? framebuffer_->height : 0;
}

SDL_GPUTexture *WebGLContext::target_depth() {
  return framebuffer_depth(bound_framebuffer_);
}

SDL_GPUTexture *WebGLContext::framebuffer_depth(uint32_t id) {
  if (id == 0) {
    ensure_default_target();
    return depth_;
  }
  WebGLFramebuffer *framebuffer = this->framebuffer(id);
  if (framebuffer == nullptr) return nullptr;
  if (framebuffer->depth_texture != 0) {
    WebGLTexture *texture = this->texture(framebuffer->depth_texture);
    if (texture == nullptr) return nullptr;
    texture->as_depth_target = true;
    return ensure_texture_gpu(texture, true);
  }
  if (framebuffer->depth == nullptr && framebuffer->depth_renderbuffer == 0 &&
      framebuffer->width > 0 && framebuffer->height > 0 && guidev::g_device != nullptr) {
    SDL_GPUTextureCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
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

int WebGLContext::target_color_format() {
  if (bound_framebuffer_ == 0) return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  WebGLFramebuffer *framebuffer = this->framebuffer(bound_framebuffer_);
  if (framebuffer == nullptr || framebuffer->color_textures[0] == 0) {
    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  }
  WebGLTexture *texture = this->texture(framebuffer->color_textures[0]);
  return texture != nullptr ? (int)texture->format : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

int WebGLContext::target_depth_format() {
  if (bound_framebuffer_ == 0) return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
  WebGLFramebuffer *framebuffer = this->framebuffer(bound_framebuffer_);
  if (framebuffer == nullptr) return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
  if (framebuffer->depth_texture != 0) {
    WebGLTexture *texture = this->texture(framebuffer->depth_texture);
    if (texture != nullptr) return (int)texture->format;
  }
  return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
}

void WebGLContext::end_pass() {
  if (pass_ != nullptr && cmd_ != nullptr) {
    SDL_EndGPURenderPass(pass_);
    pass_ = nullptr;
  }
}

void WebGLContext::begin_pass(bool clear_color, bool clear_depth, bool clear_stencil) {
  if (guidev::g_device == nullptr) return;
  if (pass_ != nullptr && pass_targets_default_ == (bound_framebuffer_ == 0) &&
      pass_framebuffer_ == bound_framebuffer_ && !clear_color && !clear_depth &&
      !clear_stencil) {
    return;
  }
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return;
  SDL_GPUTexture *depth = target_depth();
  SDL_GPUColorTargetInfo color_infos[kWebGLMaxDrawBuffers];
  int color_count = 0;
  for (int slot = 0; slot < kWebGLMaxDrawBuffers; slot++) {
    SDL_GPUTexture *color = draw_color_texture(slot);
    if (color == nullptr) continue;
    SDL_GPUColorTargetInfo &color_info = color_infos[color_count++];
    memset(&color_info, 0, sizeof(color_info));
    color_info.texture = color;
    color_info.layer_or_depth_plane = (Uint32)draw_color_layer(slot);
    color_info.clear_color.r = clear_color_[0];
    color_info.clear_color.g = clear_color_[1];
    color_info.clear_color.b = clear_color_[2];
    color_info.clear_color.a = clear_color_[3];
    color_info.load_op = clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    color_info.store_op = SDL_GPU_STOREOP_STORE;
  }
  if (color_count == 0) {
    set_error(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }

  SDL_GPUDepthStencilTargetInfo depth_info;
  memset(&depth_info, 0, sizeof(depth_info));
  depth_info.texture = depth;
  depth_info.clear_depth = clear_depth_;
  depth_info.load_op = (clear_depth && depth != nullptr) ? SDL_GPU_LOADOP_CLEAR
                                                         : SDL_GPU_LOADOP_LOAD;
  depth_info.store_op = SDL_GPU_STOREOP_STORE;
  depth_info.clear_stencil = (Uint8)(clear_stencil_ & 0xFF);
  depth_info.stencil_load_op = (clear_stencil && depth != nullptr) ? SDL_GPU_LOADOP_CLEAR
                                                                  : SDL_GPU_LOADOP_LOAD;
  depth_info.stencil_store_op = SDL_GPU_STOREOP_STORE;

  pass_ = SDL_BeginGPURenderPass(cmd_, color_infos, (Uint32)color_count,
                                 depth != nullptr ? &depth_info : nullptr);
  pass_targets_default_ = bound_framebuffer_ == 0;
  pass_framebuffer_ = bound_framebuffer_;
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
      /* The vertex stage binds its own sampler set (`SDL_BindGPUVertexSamplers`)
       * and the translator emits vertex samplers in descriptor set 0. */
      return kWebGLMaxTextureUnits;
    case GL_MAX_VERTEX_UNIFORM_VECTORS:
    case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
    case GL_MAX_VARYING_VECTORS:
      return 256;
    case GL_MAX_RENDERBUFFER_SIZE:
      return 8192;
    case GL_MAX_VIEWPORT_DIMS:
      return 8192;
    case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
      return 8192;
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
      return 8;
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
      return kWebGLMaxDrawBuffers;
    case GL_READ_BUFFER:
      return read_buffer_;
    case GL_DRAW_BUFFER0:
      return draw_buffer_value(0);
    case GL_DRAW_BUFFER1:
      return draw_buffer_value(1);
    case GL_DRAW_BUFFER2:
      return draw_buffer_value(2);
    case GL_DRAW_BUFFER3:
      return draw_buffer_value(3);
    case GL_MAX_SAMPLES:
      return 0;
    case GL_MAX_UNIFORM_BUFFER_BINDINGS:
    case GL_MAX_VERTEX_UNIFORM_BLOCKS:
    case GL_MAX_FRAGMENT_UNIFORM_BLOCKS:
      return 16;
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
  const int target = active_texture_target_[unit];
  if (target == GL_TEXTURE_CUBE_MAP) return bound_cube_textures_[unit];
  if (target == GL_TEXTURE_2D_ARRAY) return bound_array_textures_[unit];
  if (target == GL_TEXTURE_3D) return bound_3d_textures_[unit];
  return bound_textures_[unit];
}

uint32_t WebGLContext::bound_cube_texture(int unit) const {
  if (unit < 0 || unit >= kWebGLMaxTextureUnits) return 0;
  return bound_cube_textures_[unit];
}

uint32_t WebGLContext::bound_array_texture(int unit) const {
  if (unit < 0 || unit >= kWebGLMaxTextureUnits) return 0;
  return bound_array_textures_[unit];
}

uint32_t WebGLContext::bound_3d_texture(int unit) const {
  if (unit < 0 || unit >= kWebGLMaxTextureUnits) return 0;
  return bound_3d_textures_[unit];
}

void WebGLContext::viewport_box(int out[4]) const {
  for (int i = 0; i < 4; i++) out[i] = viewport_[i];
}

void WebGLContext::scissor_box(int out[4]) const {
  for (int i = 0; i < 4; i++) out[i] = scissor_box_[i];
}

}  // namespace xtgui
