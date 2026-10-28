/*
 * xbintsc GUI engine — WebGL texture objects and framebuffers.
 *
 * Uploads are recorded immediately: a copy pass cannot run inside a render
 * pass, so a texture update ends the open pass first. The next draw starts a
 * new pass with `LOAD`, so the framebuffer keeps its contents. Buffer objects
 * and vertex-attribute state live in `webgl_buffer_objects.cpp`.
 */

#include "webgl.h"

#include <algorithm>
#include <cstring>

#include "gui_internal.h"

namespace xtgui {
namespace {

int mip_levels_for(int width, int height) {
  int levels = 1;
  int size = std::max(width, height);
  while (size > 1) {
    size /= 2;
    levels++;
  }
  return levels;
}

SDL_GPUFilter filter_of(int gl_filter) {
  return gl_filter == GL_NEAREST || gl_filter == GL_NEAREST_MIPMAP_NEAREST ||
                 gl_filter == GL_NEAREST_MIPMAP_LINEAR
             ? SDL_GPU_FILTER_NEAREST
             : SDL_GPU_FILTER_LINEAR;
}

bool uses_mipmaps(int gl_filter) {
  return gl_filter == GL_NEAREST_MIPMAP_NEAREST || gl_filter == GL_LINEAR_MIPMAP_NEAREST ||
         gl_filter == GL_NEAREST_MIPMAP_LINEAR || gl_filter == GL_LINEAR_MIPMAP_LINEAR;
}

bool is_depth_format(SDL_GPUTextureFormat format) {
  return format == SDL_GPU_TEXTUREFORMAT_D16_UNORM ||
         format == SDL_GPU_TEXTUREFORMAT_D24_UNORM ||
         format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT ||
         format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ||
         format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
}

SDL_GPUSamplerAddressMode address_of(int wrap) {
  switch (wrap) {
    case GL_CLAMP_TO_EDGE:
      return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    case GL_MIRRORED_REPEAT:
      return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    default:
      return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  }
}

SDL_GPUCompareOp compare_of(int func) {
  switch (func) {
    case GL_NEVER:
      return SDL_GPU_COMPAREOP_NEVER;
    case GL_LESS:
      return SDL_GPU_COMPAREOP_LESS;
    case GL_EQUAL:
      return SDL_GPU_COMPAREOP_EQUAL;
    case GL_LEQUAL:
      return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    case GL_GREATER:
      return SDL_GPU_COMPAREOP_GREATER;
    case GL_NOTEQUAL:
      return SDL_GPU_COMPAREOP_NOT_EQUAL;
    case GL_GEQUAL:
      return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
    default:
      return SDL_GPU_COMPAREOP_ALWAYS;
  }
}

}  // namespace

/* -- textures ------------------------------------------------------------ */

uint32_t WebGLContext::create_texture() {
  auto record = std::make_unique<WebGLTexture>();
  record->id = next_object_id_++;
  record->min_filter = GL_NEAREST_MIPMAP_LINEAR;
  record->mag_filter = GL_LINEAR;
  record->wrap_s = GL_REPEAT;
  record->wrap_t = GL_REPEAT;
  const uint32_t id = record->id;
  textures_[id] = std::move(record);
  return id;
}

void WebGLContext::delete_texture(uint32_t id) {
  auto found = textures_.find(id);
  if (found == textures_.end()) return;
  for (uint32_t &bound : bound_textures_) {
    if (bound == id) bound = 0;
  }
  for (uint32_t &bound : bound_cube_textures_) {
    if (bound == id) bound = 0;
  }
  for (uint32_t &bound : bound_array_textures_) {
    if (bound == id) bound = 0;
  }
  for (uint32_t &bound : bound_3d_textures_) {
    if (bound == id) bound = 0;
  }
  for (auto &entry : framebuffers_) {
    bool cleared = false;
    for (uint32_t &color : entry.second->color_textures) {
      if (color == id) {
        color = 0;
        cleared = true;
      }
    }
    if (cleared) {
      entry.second->width = 0;
      entry.second->height = 0;
    }
    if (entry.second->depth_texture == id) entry.second->depth_texture = 0;
  }
  if (guidev::g_device != nullptr) {
    if (found->second->gpu != nullptr) {
      submit(); SDL_WaitForGPUIdle(guidev::g_device);
      SDL_ReleaseGPUTexture(guidev::g_device, found->second->gpu);
    }
    if (found->second->sampler != nullptr) {
      SDL_ReleaseGPUSampler(guidev::g_device, found->second->sampler);
    }
    if (found->second->compare_sampler != nullptr) {
      SDL_ReleaseGPUSampler(guidev::g_device, found->second->compare_sampler);
    }
  }
  textures_.erase(found);
}

void WebGLContext::bind_texture(int target, uint32_t id) {
  if (id != 0 && textures_.find(id) == textures_.end()) return;
  uint32_t *slot = nullptr;
  if (target == GL_TEXTURE_2D) {
    slot = &bound_textures_[active_unit_];
  } else if (target == GL_TEXTURE_CUBE_MAP) {
    slot = &bound_cube_textures_[active_unit_];
  } else if (target == GL_TEXTURE_2D_ARRAY) {
    slot = &bound_array_textures_[active_unit_];
  } else if (target == GL_TEXTURE_3D) {
    slot = &bound_3d_textures_[active_unit_];
  } else {
    return;
  }
  *slot = id;
  active_texture_target_[active_unit_] = target;
  if (id != 0) {
    WebGLTexture *record = texture(id);
    if (record != nullptr) {
      if (!record->defined && record->gpu == nullptr) {
        record->target = target;
      } else if (record->target != target) {
        *slot = 0;
        return;
      }
    }
  }
}

void WebGLContext::active_texture(int unit) {
  if (unit < GL_TEXTURE0 || unit >= GL_TEXTURE0 + kWebGLMaxTextureUnits) {
    set_error(GL_INVALID_ENUM);
    return;
  }
  active_unit_ = unit - GL_TEXTURE0;
}

bool WebGLContext::tex_parameter(uint32_t id, int pname, int value) {
  WebGLTexture *record = texture(id);
  if (record == nullptr) return false;
  if (pname == GL_TEXTURE_MIN_FILTER) record->min_filter = value;
  else if (pname == GL_TEXTURE_MAG_FILTER) record->mag_filter = value;
  else if (pname == GL_TEXTURE_WRAP_S) record->wrap_s = value;
  else if (pname == GL_TEXTURE_WRAP_T) record->wrap_t = value;
  else if (pname == GL_TEXTURE_COMPARE_MODE) {
    record->compare_mode = value == GL_COMPARE_REF_TO_TEXTURE;
  } else if (pname == GL_TEXTURE_COMPARE_FUNC) {
    record->compare_func = value;
  } else return true;  // other parameters are accepted and ignored
  if (record->sampler != nullptr && guidev::g_device != nullptr) {
    SDL_ReleaseGPUSampler(guidev::g_device, record->sampler);
    record->sampler = nullptr;
  }
  if (record->compare_sampler != nullptr && guidev::g_device != nullptr) {
    SDL_ReleaseGPUSampler(guidev::g_device, record->compare_sampler);
    record->compare_sampler = nullptr;
  }
  return true;
}

int WebGLContext::tex_parameter_value(uint32_t id, int pname) {
  WebGLTexture *record = texture(id);
  if (record == nullptr) return 0;
  if (pname == GL_TEXTURE_COMPARE_MODE) {
    return record->compare_mode ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE;
  }
  if (pname == GL_TEXTURE_COMPARE_FUNC) return record->compare_func;
  if (pname == GL_TEXTURE_MIN_FILTER) return record->min_filter;
  if (pname == GL_TEXTURE_MAG_FILTER) return record->mag_filter;
  if (pname == GL_TEXTURE_WRAP_S) return record->wrap_s;
  if (pname == GL_TEXTURE_WRAP_T) return record->wrap_t;
  return 0;
}

SDL_GPUTexture *WebGLContext::ensure_texture_gpu(WebGLTexture *record, bool as_target) {
  if (record == nullptr || guidev::g_device == nullptr) return nullptr;
  if (record->width <= 0 || record->height <= 0) return record->gpu;
  const bool depth = is_depth_format(record->format);
  record->depth = depth;
  /* Allocate sample *and* target usage up front: attaching a texture that was
   * already uploaded must not recreate it (shadow maps upload or attach in
   * either order). Compressed textures are sample-only; layered textures can
   * still be colour targets (a single layer is selected at render time). */
  const uint32_t wanted =
      record->compressed
          ? SDL_GPU_TEXTUREUSAGE_SAMPLER
          : (SDL_GPU_TEXTUREUSAGE_SAMPLER |
             (depth ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                    : SDL_GPU_TEXTUREUSAGE_COLOR_TARGET));
  if (record->gpu != nullptr && record->usage == wanted) {
    if (as_target) {
      if (depth) record->as_depth_target = true;
      else if (!record->compressed) record->as_target = true;
    }
    return record->gpu;
  }
  if (record->gpu != nullptr) {
    submit(); SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, record->gpu);
    record->gpu = nullptr;
  }
  record->as_target = as_target && !depth && !record->compressed;
  record->as_depth_target = as_target && depth;
  record->usage = wanted;
  record->levels =
      (depth || record->compressed) ? 1 : mip_levels_for(record->width, record->height);
  SDL_GPUTextureType texture_type = SDL_GPU_TEXTURETYPE_2D;
  int layers = 1;
  if (record->target == GL_TEXTURE_CUBE_MAP) {
    texture_type = SDL_GPU_TEXTURETYPE_CUBE;
    layers = 6;
  } else if (record->target == GL_TEXTURE_2D_ARRAY) {
    texture_type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    layers = record->layers > 0 ? record->layers : 1;
  } else if (record->target == GL_TEXTURE_3D) {
    texture_type = SDL_GPU_TEXTURETYPE_3D;
    layers = record->layers > 0 ? record->layers : 1;
  }
  SDL_GPUTextureCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.type = texture_type;
  info.format = record->format;
  info.usage = wanted;
  info.width = (Uint32)record->width;
  info.height = (Uint32)record->height;
  info.layer_count_or_depth = (Uint32)layers;
  info.num_levels = (Uint32)record->levels;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  record->gpu = SDL_CreateGPUTexture(guidev::g_device, &info);
  if (record->gpu == nullptr && depth) {
    /* Some backends cannot sample a depth-stencil format; an attachment-only
     * texture is still useful for depth-only rendering. */
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    record->gpu = SDL_CreateGPUTexture(guidev::g_device, &info);
    record->usage = info.usage;
  }
  if (record->gpu == nullptr) set_error(GL_OUT_OF_MEMORY);
  return record->gpu;
}

SDL_GPUSampler *WebGLContext::sampler_for(WebGLTexture *record) {
  if (record == nullptr || guidev::g_device == nullptr) return nullptr;
  if (record->sampler != nullptr) return record->sampler;
  SDL_GPUSamplerCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.min_filter = filter_of(record->min_filter);
  info.mag_filter = filter_of(record->mag_filter);
  info.mipmap_mode = uses_mipmaps(record->min_filter) ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                                                      : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  info.address_mode_u = address_of(record->wrap_s);
  info.address_mode_v = address_of(record->wrap_t);
  info.address_mode_w = address_of(record->wrap_t);
  info.min_lod = 0.0f;
  info.max_lod = record->has_mipmaps ? 1000.0f : 0.0f;
  if (record->depth) {
    /* Depth formats are sampled unfiltered; a depth value has no meaning once
     * interpolated across a texel boundary. */
    info.min_filter = SDL_GPU_FILTER_NEAREST;
    info.mag_filter = SDL_GPU_FILTER_NEAREST;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.max_lod = 0.0f;
  }
  record->sampler = SDL_CreateGPUSampler(guidev::g_device, &info);
  return record->sampler;
}

SDL_GPUSampler *WebGLContext::sampler_for_compare(WebGLTexture *record) {
  if (record == nullptr || guidev::g_device == nullptr) return nullptr;
  if (record->compare_sampler != nullptr) return record->compare_sampler;
  SDL_GPUSamplerCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.min_filter = SDL_GPU_FILTER_LINEAR;
  info.mag_filter = SDL_GPU_FILTER_LINEAR;
  info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  info.address_mode_u = address_of(record->wrap_s);
  info.address_mode_v = address_of(record->wrap_t);
  info.address_mode_w = address_of(record->wrap_t);
  info.min_lod = 0.0f;
  info.max_lod = record->has_mipmaps ? 1000.0f : 0.0f;
  info.enable_compare = true;
  info.compare_op = compare_of(record->compare_func);
  record->compare_sampler = SDL_CreateGPUSampler(guidev::g_device, &info);
  return record->compare_sampler;
}

int WebGLContext::texture_width(uint32_t id) {
  WebGLTexture *record = texture(id);
  return record != nullptr ? record->width : 0;
}

int WebGLContext::texture_height(uint32_t id) {
  WebGLTexture *record = texture(id);
  return record != nullptr ? record->height : 0;
}

int WebGLContext::texture_format(uint32_t id) {
  WebGLTexture *record = texture(id);
  return record != nullptr ? (int)record->format : 0;
}

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
  if (target == GL_FRAMEBUFFER) read_framebuffer_ = id;
  read_buffer_ = id == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0;
}

bool WebGLContext::framebuffer_texture(uint32_t framebuffer_id, uint32_t texture_id,
                                       int attachment) {
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
      if (record_framebuffer->width <= 0 || record_framebuffer->height <= 0) {
        record_framebuffer->width = record->width;
        record_framebuffer->height = record->height;
      }
    }
    return true;
  }
  const int color_index = attachment - GL_COLOR_ATTACHMENT0;
  if (color_index < 0 || color_index >= kWebGLMaxDrawBuffers) return false;
  record_framebuffer->color_textures[color_index] = texture_id;
  if (record != nullptr) {
    record_framebuffer->width = record->width;
    record_framebuffer->height = record->height;
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

bool WebGLContext::framebuffer_status(uint32_t id) {
  WebGLFramebuffer *framebuffer_ = framebuffer(id);
  if (framebuffer_ == nullptr) return false;
  bool attached = false;
  for (uint32_t color : framebuffer_->color_textures) attached = attached || color != 0;
  if (!attached) {
    set_error(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT);
    return false;
  }
  if (framebuffer_->width <= 0 || framebuffer_->height <= 0) return false;
  return true;
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
  }
  if (found->second->depth != nullptr && guidev::g_device != nullptr) {
    submit(); SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, found->second->depth);
  }
  renderbuffers_.erase(found);
}

bool WebGLContext::renderbuffer_storage(uint32_t id, int width, int height, bool stencil) {
  WebGLRenderbuffer *record = renderbuffer(id);
  if (record == nullptr || width <= 0 || height <= 0) return false;
  record->width = width;
  record->height = height;
  record->stencil = stencil;
  if (record->depth != nullptr) {
    submit(); SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, record->depth);
    record->depth = nullptr;
  }
  SDL_GPUTextureCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.type = SDL_GPU_TEXTURETYPE_2D;
  /* Every depth target uses the same depth-stencil format so one pipeline
   * works with the default framebuffer and with render-to-texture targets;
   * `stencil` only records whether the page asked for a stencil attachment. */
  info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
  info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
  info.width = (Uint32)width;
  info.height = (Uint32)height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  record->depth = SDL_CreateGPUTexture(guidev::g_device, &info);
  return record->depth != nullptr;
}

bool WebGLContext::framebuffer_renderbuffer(uint32_t framebuffer_id, uint32_t renderbuffer_id) {
  WebGLFramebuffer *framebuffer_ = framebuffer(framebuffer_id);
  WebGLRenderbuffer *record = renderbuffer(renderbuffer_id);
  if (framebuffer_ == nullptr) return false;
  if (renderbuffer_id != 0 && record == nullptr) return false;
  framebuffer_->depth_renderbuffer = renderbuffer_id;
  framebuffer_->depth = record != nullptr ? record->depth : nullptr;
  return true;
}

/* -- lookups ------------------------------------------------------------- */

WebGLShader *WebGLContext::shader(uint32_t id) {
  auto found = shaders_.find(id);
  return found == shaders_.end() ? nullptr : found->second.get();
}

WebGLProgram *WebGLContext::program(uint32_t id) {
  auto found = programs_.find(id);
  return found == programs_.end() ? nullptr : found->second.get();
}

WebGLBuffer *WebGLContext::buffer(uint32_t id) {
  auto found = buffers_.find(id);
  return found == buffers_.end() ? nullptr : found->second.get();
}

WebGLTexture *WebGLContext::texture(uint32_t id) {
  auto found = textures_.find(id);
  return found == textures_.end() ? nullptr : found->second.get();
}

WebGLFramebuffer *WebGLContext::framebuffer(uint32_t id) {
  auto found = framebuffers_.find(id);
  return found == framebuffers_.end() ? nullptr : found->second.get();
}

WebGLRenderbuffer *WebGLContext::renderbuffer(uint32_t id) {
  auto found = renderbuffers_.find(id);
  return found == renderbuffers_.end() ? nullptr : found->second.get();
}

}  // namespace xtgui
