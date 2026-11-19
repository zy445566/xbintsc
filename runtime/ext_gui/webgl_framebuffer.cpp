/*
 * xbintsc GUI engine — WebGL framebuffer and renderbuffer objects.
 *
 * A framebuffer is a set of up to four colour attachments (texture or
 * renderbuffer), an optional depth/stencil attachment and the draw-buffer
 * table, plus an auto-created depth buffer when the page attaches none.
 * Renderbuffers are engine-owned render targets so a page can ask for a
 * multisampled colour or depth buffer without a texture. Completeness
 * (dimensions and sample counts) is reported with the exact GL status enum.
 */

#include "webgl.h"

#include <cstdio>
#include <cstring>

#include "gui_internal.h"

namespace xtgui {
namespace {

/** `XT_GUI_TRACE=1` explains an incomplete framebuffer. */
bool trace_enabled() {
  static int value = -1;
  if (value < 0) {
    const char *env = SDL_getenv("XT_GUI_TRACE");
    value = (env != nullptr && env[0] != '\0' && env[0] != '0') ? 1 : 0;
  }
  return value != 0;
}

}  // namespace

/* -- framebuffers -------------------------------------------------------- */

uint32_t WebGLContext::create_framebuffer() {
  auto record = std::make_unique<WebGLFramebuffer>();
  record->id = next_object_id_++;
  const uint32_t id = record->id;
  framebuffers_[id] = std::move(record);
  return id;
}

void WebGLContext::delete_framebuffer(uint32_t id) {
  auto found = framebuffers_.find(id);
  if (found == framebuffers_.end()) return;
  if (bound_framebuffer_ == id) bind_framebuffer(GL_FRAMEBUFFER, 0);
  if (read_framebuffer_ == id) read_framebuffer_ = 0;
  if (found->second->depth != nullptr && found->second->depth_renderbuffer == 0 &&
      guidev::g_device != nullptr) {
    submit(); SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, found->second->depth);
  }
  framebuffers_.erase(found);
}

void WebGLContext::bind_framebuffer(int target, uint32_t id) {
  if (id != 0 && framebuffers_.find(id) == framebuffers_.end()) return;
  if (target == GL_READ_FRAMEBUFFER) {
    read_framebuffer_ = id;
    read_buffer_ = id == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0;
    return;
  }
  if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER) return;
  if (id != bound_framebuffer_) end_pass();
  bound_framebuffer_ = id;
  framebuffer_version_ += 1;
  if (target == GL_FRAMEBUFFER) read_framebuffer_ = id;
  read_buffer_ = id == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0;
}

bool WebGLContext::framebuffer_texture(uint32_t framebuffer_id, uint32_t texture_id,
                                       int attachment) {
  framebuffer_version_ += 1;
  WebGLFramebuffer *record_framebuffer = framebuffer(framebuffer_id);
  WebGLTexture *record = texture(texture_id);
  if (record_framebuffer == nullptr) return false;
  if (texture_id != 0 && record == nullptr) return false;
  if (attachment == GL_DEPTH_ATTACHMENT || attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
    record_framebuffer->depth_texture = texture_id;
    record_framebuffer->depth_renderbuffer = 0;
    if (record_framebuffer->depth != nullptr) {
      submit(); SDL_WaitForGPUIdle(guidev::g_device);
      SDL_ReleaseGPUTexture(guidev::g_device, record_framebuffer->depth);
      record_framebuffer->depth = nullptr;
    }
    if (record != nullptr) {
      record->as_depth_target = true;
      ensure_texture_gpu(record, true);
      /* Storage may not exist yet; color_attachment and framebuffer_size re-read
       * the real size once it does, so never clobber a known size with 0. */
      if (record->width > 0 && record->height > 0) {
        record_framebuffer->width = record->width;
        record_framebuffer->height = record->height;
      }
    }
    return true;
  }
  const int color_index = attachment - GL_COLOR_ATTACHMENT0;
  if (color_index < 0 || color_index >= kWebGLMaxDrawBuffers) return false;
  record_framebuffer->color_textures[color_index] = texture_id;
  record_framebuffer->color_renderbuffers[color_index] = 0;
  if (record != nullptr) {
    if (record->width > 0 && record->height > 0) {
      record_framebuffer->width = record->width;
      record_framebuffer->height = record->height;
    }
    ensure_texture_gpu(record, true);
  }
  return true;
}

bool WebGLContext::framebuffer_texture_layer(uint32_t framebuffer_id, uint32_t texture_id,
                                             int attachment, int layer) {
  if (!framebuffer_texture(framebuffer_id, texture_id, attachment)) return false;
  const int index = attachment - GL_COLOR_ATTACHMENT0;
  WebGLFramebuffer *record = framebuffer(framebuffer_id);
  if (record != nullptr && index >= 0 && index < kWebGLMaxDrawBuffers) {
    record->color_layers[index] = layer;
  }
  /* SDL_GPU 3.2 depth-stencil targets have no layer selector; the depth
   * attachment still binds, without per-layer rendering. */
  return true;
}

int WebGLContext::framebuffer_status(uint32_t id) {
  WebGLFramebuffer *framebuffer_ = framebuffer(id);
  if (framebuffer_ == nullptr) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
  auto incomplete = [&](int status, const char *why, int slot, uint32_t attachment) {
    if (trace_enabled()) {
      WebGLTexture *attachment_texture = attachment != 0 ? texture(attachment) : nullptr;
      fprintf(stderr,
              "xt_gui: fb %u incomplete (%d): %s slot=%d attachment=%u texture=%p texsize=%dx%d "
              "texgpu=%p fbsize=%dx%d\n",
              id, status, why, slot, attachment, (void *)attachment_texture,
              attachment_texture != nullptr ? attachment_texture->width : -1,
              attachment_texture != nullptr ? attachment_texture->height : -1,
              attachment_texture != nullptr ? (void *)attachment_texture->gpu : nullptr,
              framebuffer_->width, framebuffer_->height);
    }
    return status;
  };
  bool attached = false;
  int width = 0;
  int height = 0;
  int samples = 1;
  if (trace_enabled()) {
    fprintf(stderr,
            "xt_gui: fb %u status: color=[%u,%u,%u,%u] rb=[%u,%u,%u,%u] depth=%u/%u size=%dx%d\n",
            id, framebuffer_->color_textures[0], framebuffer_->color_textures[1],
            framebuffer_->color_textures[2], framebuffer_->color_textures[3],
            framebuffer_->color_renderbuffers[0], framebuffer_->color_renderbuffers[1],
            framebuffer_->color_renderbuffers[2], framebuffer_->color_renderbuffers[3],
            framebuffer_->depth_texture, framebuffer_->depth_renderbuffer, framebuffer_->width,
            framebuffer_->height);
  }
  for (int index = 0; index < kWebGLMaxDrawBuffers; index++) {
    if (framebuffer_->color_textures[index] == 0 &&
        framebuffer_->color_renderbuffers[index] == 0) {
      continue;
    }
    int format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    int attachment_samples = 1;
    int attachment_width = 0;
    int attachment_height = 0;
    if (color_attachment(id, index, &format, &attachment_samples, &attachment_width,
                         &attachment_height) == nullptr) {
      return incomplete(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT, "colour attachment unresolved",
                        index, framebuffer_->color_textures[index]);
    }
    if (!attached) {
      attached = true;
      width = attachment_width;
      height = attachment_height;
      samples = attachment_samples;
    } else if (attachment_samples != samples) {
      return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
    } else if (attachment_width != width || attachment_height != height) {
      return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
    }
  }
  if (framebuffer_->depth_renderbuffer != 0 || framebuffer_->depth_texture != 0 ||
      framebuffer_->depth != nullptr) {
    int depth_samples = 1;
    int depth_width = framebuffer_->width;
    int depth_height = framebuffer_->height;
    if (framebuffer_->depth_renderbuffer != 0) {
      WebGLRenderbuffer *renderbuffer_ = renderbuffer(framebuffer_->depth_renderbuffer);
      if (renderbuffer_ == nullptr) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
      depth_samples = renderbuffer_->samples;
      depth_width = renderbuffer_->width;
      depth_height = renderbuffer_->height;
    } else if (framebuffer_->depth_texture != 0) {
      WebGLTexture *texture_ = texture(framebuffer_->depth_texture);
      if (texture_ == nullptr) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
      depth_samples = texture_->samples;
      depth_width = texture_->width;
      depth_height = texture_->height;
    }
    if (!attached) {
      attached = true;
      width = depth_width;
      height = depth_height;
      samples = depth_samples;
    } else if (depth_samples != samples) {
      return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
    } else if (depth_width != width || depth_height != height) {
      return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
    }
  }
  if (!attached) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
  if (width <= 0 || height <= 0) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
  return GL_FRAMEBUFFER_COMPLETE;
}

uint32_t WebGLContext::create_renderbuffer() {
  auto record = std::make_unique<WebGLRenderbuffer>();
  record->id = next_object_id_++;
  const uint32_t id = record->id;
  renderbuffers_[id] = std::move(record);
  return id;
}

void WebGLContext::delete_renderbuffer(uint32_t id) {
  auto found = renderbuffers_.find(id);
  if (found == renderbuffers_.end()) return;
  if (bound_renderbuffer_ == id) bound_renderbuffer_ = 0;
  for (auto &entry : framebuffers_) {
    if (entry.second->depth_renderbuffer == id) {
      entry.second->depth_renderbuffer = 0;
      entry.second->depth = nullptr;
    }
    for (uint32_t &color : entry.second->color_renderbuffers) {
      if (color == id) color = 0;
    }
  }
  if (found->second->gpu != nullptr && guidev::g_device != nullptr) {
    submit(); SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, found->second->gpu);
  }
  renderbuffers_.erase(found);
}

bool WebGLContext::renderbuffer_storage(uint32_t id, int width, int height, int internalformat,
                                        int samples) {
  WebGLRenderbuffer *record = renderbuffer(id);
  if (record == nullptr) return false;
  if (width <= 0 || height <= 0) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  if (samples != 0 && samples != 1 && samples != 2 && samples != kWebGLMaxSamples) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  const bool depth = internalformat == GL_DEPTH_COMPONENT ||
                     internalformat == GL_DEPTH_COMPONENT16 ||
                     internalformat == GL_DEPTH_COMPONENT24 ||
                     internalformat == GL_DEPTH_COMPONENT32F ||
                     internalformat == GL_DEPTH_STENCIL ||
                     internalformat == GL_DEPTH24_STENCIL8 ||
                     internalformat == GL_DEPTH32F_STENCIL8 ||
                     internalformat == GL_STENCIL_INDEX8;
  /* One depth-stencil format serves every depth renderbuffer, so a pipeline
   * built for the default framebuffer also renders into a user one. */
  const SDL_GPUTextureFormat format =
      depth ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT : webgl_storage_format(internalformat);
  int granted_samples = samples > 1 ? samples : 1;
  if (granted_samples > 1 &&
      (!multisample_supported_ ||
       !SDL_GPUTextureSupportsSampleCount(guidev::g_device, format,
                                          webgl_sample_count(granted_samples)))) {
    /* The backend cannot multisample this target. Degrade to the same
     * single-sampled storage a plain `renderbufferStorage` would allocate, so a
     * page that asks for MSAA still renders (without antialiasing) instead of
     * losing the attachment; `RENDERBUFFER_SAMPLES` then reports 0. */
    granted_samples = 1;
  }
  if (record->gpu != nullptr) {
    submit(); SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, record->gpu);
    record->gpu = nullptr;
  }
  record->width = width;
  record->height = height;
  record->internalformat = internalformat;
  record->samples = granted_samples;
  record->depth = depth;
  record->format = format;
  SDL_GPUTextureCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = format;
  /* A single-sampled colour renderbuffer is also a valid blit source, so it
   * carries SAMPLER usage; a multisampled one cannot be sampled and is
   * resolved through a render pass instead. */
  info.usage = depth ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                     : (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                        (record->samples > 1 ? 0 : SDL_GPU_TEXTUREUSAGE_SAMPLER));
  info.width = (Uint32)width;
  info.height = (Uint32)height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = webgl_sample_count(record->samples);
  record->gpu = SDL_CreateGPUTexture(guidev::g_device, &info);
  if (record->gpu == nullptr) {
    set_error(GL_OUT_OF_MEMORY);
    return false;
  }
  return true;
}

bool WebGLContext::framebuffer_renderbuffer(uint32_t framebuffer_id, int attachment,
                                            uint32_t renderbuffer_id) {
  framebuffer_version_ += 1;
  WebGLFramebuffer *framebuffer_ = framebuffer(framebuffer_id);
  WebGLRenderbuffer *record = renderbuffer(renderbuffer_id);
  if (framebuffer_ == nullptr) return false;
  if (renderbuffer_id != 0 && record == nullptr) return false;
  if (attachment == GL_DEPTH_ATTACHMENT || attachment == GL_DEPTH_STENCIL_ATTACHMENT ||
      attachment == GL_STENCIL_ATTACHMENT) {
    framebuffer_->depth_renderbuffer = renderbuffer_id;
    framebuffer_->depth_texture = 0;
    if (framebuffer_->depth != nullptr) {
      submit(); SDL_WaitForGPUIdle(guidev::g_device);
      SDL_ReleaseGPUTexture(guidev::g_device, framebuffer_->depth);
      framebuffer_->depth = nullptr;
    }
    if (record != nullptr && record->width > 0 && record->height > 0) {
      framebuffer_->width = record->width;
      framebuffer_->height = record->height;
    }
    return true;
  }
  const int index = attachment - GL_COLOR_ATTACHMENT0;
  if (index < 0 || index >= kWebGLMaxDrawBuffers) return false;
  framebuffer_->color_renderbuffers[index] = renderbuffer_id;
  framebuffer_->color_textures[index] = 0;
  if (record != nullptr && record->width > 0 && record->height > 0) {
    framebuffer_->width = record->width;
    framebuffer_->height = record->height;
  }
  return true;
}

}  // namespace xtgui
