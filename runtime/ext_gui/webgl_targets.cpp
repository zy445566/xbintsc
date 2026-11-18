/*
 * xbintsc GUI engine — WebGL render targets, resolves and render passes.
 *
 * A context renders either into the multisampled default drawing buffer (which
 * every pass resolves into the composited colour texture), into a
 * single-sampled default target, or into a user framebuffer whose colour and
 * depth attachments may themselves be multisampled. This unit owns the target
 * lookup, the scratch resolve texture, `readPixels`'s resolved source and the
 * render pass itself; lifetime and object tables stay in `webgl_context.cpp`.
 */

#include "webgl.h"

#include <algorithm>
#include <cstring>

#include "gui_internal.h"
#include "perf.h"

namespace xtgui {

/* -- passes -------------------------------------------------------------- */

SDL_GPUTexture *WebGLContext::target_color() { return draw_color_texture(0); }

SDL_GPUTexture *WebGLContext::color_attachment(uint32_t id, int index, int *format,
                                               int *samples, int *width, int *height) {
  *format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  *samples = 1;
  if (index < 0 || index >= kWebGLMaxDrawBuffers) return nullptr;
  if (id == 0) {
    ensure_default_target();
    *width = width_;
    *height = height_;
    return color_;
  }
  WebGLFramebuffer *framebuffer_ = framebuffer(id);
  if (framebuffer_ == nullptr) return nullptr;
  *width = framebuffer_->width;
  *height = framebuffer_->height;
  if (framebuffer_->color_textures[index] != 0) {
    WebGLTexture *texture_ = texture(framebuffer_->color_textures[index]);
    if (texture_ == nullptr) return nullptr;
    *format = (int)texture_->format;
    *samples = texture_->samples;
    return ensure_texture_gpu(texture_, true);
  }
  if (framebuffer_->color_renderbuffers[index] != 0) {
    WebGLRenderbuffer *renderbuffer_ = renderbuffer(framebuffer_->color_renderbuffers[index]);
    if (renderbuffer_ == nullptr) return nullptr;
    *format = (int)renderbuffer_->format;
    *samples = renderbuffer_->samples;
    return renderbuffer_->gpu;
  }
  return nullptr;
}

SDL_GPUTexture *WebGLContext::framebuffer_color(uint32_t id) {
  int format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  int samples = 1;
  int width = 0;
  int height = 0;
  return color_attachment(id, 0, &format, &samples, &width, &height);
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
  if (framebuffer->depth_renderbuffer != 0) {
    WebGLRenderbuffer *renderbuffer_ = renderbuffer(framebuffer->depth_renderbuffer);
    return renderbuffer_ != nullptr ? renderbuffer_->gpu : nullptr;
  }
  if (framebuffer->depth_texture != 0) {
    WebGLTexture *texture = this->texture(framebuffer->depth_texture);
    if (texture == nullptr) return nullptr;
    texture->as_depth_target = true;
    return ensure_texture_gpu(texture, true);
  }
  if (framebuffer->depth == nullptr && framebuffer->width > 0 && framebuffer->height > 0 &&
      guidev::g_device != nullptr) {
    SDL_GPUTextureCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    info.width = (Uint32)framebuffer->width;
    info.height = (Uint32)framebuffer->height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    /* The auto depth buffer must match the colour attachment's sample count or
     * the render pass is invalid. */
    info.sample_count = webgl_sample_count(framebuffer_samples(id));
    framebuffer->depth = SDL_CreateGPUTexture(guidev::g_device, &info);
  }
  return framebuffer->depth;
}

int WebGLContext::target_color_format() {
  int format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  int samples = 1;
  int width = 0;
  int height = 0;
  return color_attachment(bound_framebuffer_, 0, &format, &samples, &width, &height) != nullptr
             ? format
             : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

int WebGLContext::target_depth_format() {
  if (bound_framebuffer_ != 0) {
    WebGLFramebuffer *framebuffer = this->framebuffer(bound_framebuffer_);
    if (framebuffer != nullptr) {
      if (framebuffer->depth_renderbuffer != 0) {
        WebGLRenderbuffer *renderbuffer_ = renderbuffer(framebuffer->depth_renderbuffer);
        if (renderbuffer_ != nullptr) return (int)renderbuffer_->format;
      }
      if (framebuffer->depth_texture != 0) {
        WebGLTexture *texture = this->texture(framebuffer->depth_texture);
        if (texture != nullptr) return (int)texture->format;
      }
    }
  }
  return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
}

int WebGLContext::draw_color_samples(int slot) {
  if (bound_framebuffer_ == 0) return default_samples_;
  WebGLFramebuffer *framebuffer_ = framebuffer(bound_framebuffer_);
  if (framebuffer_ == nullptr) return 1;
  const int index = framebuffer_->draw_buffers[slot] - GL_COLOR_ATTACHMENT0;
  if (index < 0 || index >= kWebGLMaxDrawBuffers) return 1;
  int format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  int samples = 1;
  int width = 0;
  int height = 0;
  return color_attachment(bound_framebuffer_, index, &format, &samples, &width, &height) !=
                 nullptr
             ? samples
             : 1;
}

int WebGLContext::framebuffer_samples(uint32_t id) {
  if (id == 0) return default_samples_;
  WebGLFramebuffer *framebuffer_ = framebuffer(id);
  if (framebuffer_ == nullptr) return 1;
  for (int index = 0; index < kWebGLMaxDrawBuffers; index++) {
    int format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    int samples = 1;
    int width = 0;
    int height = 0;
    if (color_attachment(id, index, &format, &samples, &width, &height) != nullptr) return samples;
  }
  return 1;
}

int WebGLContext::target_samples() {
  if (bound_framebuffer_ == 0) return default_samples_;
  for (int slot = 0; slot < kWebGLMaxDrawBuffers; slot++) {
    if (draw_color_texture(slot) != nullptr) return draw_color_samples(slot);
  }
  return 1;
}

SDL_GPUTexture *WebGLContext::ensure_resolve_target(int width, int height,
                                                    SDL_GPUTextureFormat format) {
  if (guidev::g_device == nullptr || width <= 0 || height <= 0) return nullptr;
  if (resolve_texture_ != nullptr && resolve_width_ == width && resolve_height_ == height &&
      resolve_format_ == format) {
    return resolve_texture_;
  }
  if (resolve_texture_ != nullptr) {
    submit();
    SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, resolve_texture_);
    resolve_texture_ = nullptr;
  }
  SDL_GPUTextureCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = format;
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  info.width = (Uint32)width;
  info.height = (Uint32)height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  resolve_texture_ = SDL_CreateGPUTexture(guidev::g_device, &info);
  if (resolve_texture_ == nullptr) {
    set_error(GL_OUT_OF_MEMORY);
    return nullptr;
  }
  resolve_width_ = width;
  resolve_height_ = height;
  resolve_format_ = format;
  return resolve_texture_;
}

bool WebGLContext::resolve_color_pass(SDL_GPUTexture *source, SDL_GPUTexture *destination) {
  if (source == nullptr || destination == nullptr || guidev::g_device == nullptr) return false;
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return false;
  SDL_GPUColorTargetInfo info;
  memset(&info, 0, sizeof(info));
  info.texture = source;
  info.load_op = SDL_GPU_LOADOP_LOAD;
  /* Resolve on store, and keep the multisampled contents: the source is a
   * framebuffer a later draw may still read. */
  info.store_op = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
  info.resolve_texture = destination;
  info.resolve_mip_level = 0;
  info.resolve_layer = 0;
  SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd_, &info, 1, nullptr);
  if (pass == nullptr) {
    set_error(GL_INVALID_OPERATION);
    return false;
  }
  SDL_EndGPURenderPass(pass);
  /* The pass was not opened through `begin_pass`, so the prepared-draw cache
   * must bind its pipeline again in the next pass. */
  last_setup_ = nullptr;
  last_pipeline_ = nullptr;
  return true;
}

SDL_GPUTexture *WebGLContext::read_color_texture(uint32_t id, int *width, int *height,
                                                 int *format) {
  if (width == nullptr || height == nullptr || format == nullptr) return nullptr;
  if (id == 0) {
    ensure_default_target();
    *width = width_;
    *height = height_;
    *format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    return color_;
  }
  WebGLFramebuffer *framebuffer_ = framebuffer(id);
  if (framebuffer_ == nullptr) return nullptr;
  int index = read_buffer_ - GL_COLOR_ATTACHMENT0;
  if (index < 0 || index >= kWebGLMaxDrawBuffers) index = 0;
  int samples = 1;
  SDL_GPUTexture *attachment = color_attachment(id, index, format, &samples, width, height);
  if (attachment == nullptr) return nullptr;
  if (samples <= 1) return attachment;
  SDL_GPUTexture *resolved =
      ensure_resolve_target(*width, *height, (SDL_GPUTextureFormat)*format);
  if (resolved == nullptr) return nullptr;
  if (!resolve_color_pass(attachment, resolved)) return nullptr;
  return resolved;
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
  int pass_samples = 1;
  for (int slot = 0; slot < kWebGLMaxDrawBuffers; slot++) {
    SDL_GPUTexture *color = draw_color_texture(slot);
    if (color == nullptr) continue;
    const int samples = draw_color_samples(slot);
    if (color_count > 0 && samples != pass_samples) {
      /* Vulkan requires every attachment of a pass to share its sample count;
       * the framebuffer is incomplete, so the draw cannot run. */
      set_error(GL_INVALID_FRAMEBUFFER_OPERATION);
      return;
    }
    pass_samples = samples;
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
    /* The default framebuffer resolves its multisampled colour target into the
     * texture the page composites, at the end of every pass. */
    if (bound_framebuffer_ == 0 && slot == 0 && color_msaa_ != nullptr && color_ != nullptr) {
      color_info.resolve_texture = color_;
      color_info.store_op = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
    }
  }
  if (color_count == 0) {
    set_error(GL_INVALID_FRAMEBUFFER_OPERATION);
    return;
  }
  if (depth != nullptr) {
    int depth_samples = 1;
    if (bound_framebuffer_ == 0) {
      depth_samples = default_samples_;
    } else {
      WebGLFramebuffer *framebuffer_ = framebuffer(bound_framebuffer_);
      if (framebuffer_ != nullptr && framebuffer_->depth_renderbuffer != 0) {
        WebGLRenderbuffer *renderbuffer_ = renderbuffer(framebuffer_->depth_renderbuffer);
        if (renderbuffer_ != nullptr) depth_samples = renderbuffer_->samples;
      } else if (framebuffer_ != nullptr && framebuffer_->depth_texture != 0) {
        WebGLTexture *texture_ = texture(framebuffer_->depth_texture);
        if (texture_ != nullptr) depth_samples = texture_->samples;
      } else {
        /* `framebuffer_depth` just created the auto depth buffer with the
         * colour attachment's sample count. */
        depth_samples = pass_samples;
      }
    }
    if (depth_samples != pass_samples) {
      set_error(GL_INVALID_FRAMEBUFFER_OPERATION);
      return;
    }
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
  if (pass_ != nullptr) {
    g_perf_frame.webglPasses += 1;
    pass_serial_ += 1;
  }
  pass_targets_default_ = bound_framebuffer_ == 0;
  pass_framebuffer_ = bound_framebuffer_;
}

}  // namespace xtgui