/*
 * xbintsc GUI engine — WebGL buffers, vertex-attribute state, texture objects
 * and framebuffers.
 *
 * Uploads are recorded immediately: a copy pass cannot run inside a render
 * pass, so a buffer/texture update ends the open pass first. The next draw
 * starts a new pass with `LOAD`, so the framebuffer keeps its contents.
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

bool upload_transfer(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd, SDL_GPUBuffer *buffer,
                     const void *bytes, uint32_t size) {
  if (size == 0) return true;
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
  memcpy(mapped, bytes, size);
  SDL_UnmapGPUTransferBuffer(device, transfer);
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTransferBufferLocation source;
  memset(&source, 0, sizeof(source));
  source.transfer_buffer = transfer;
  SDL_GPUBufferRegion region;
  memset(&region, 0, sizeof(region));
  region.buffer = buffer;
  region.size = size;
  SDL_UploadToGPUBuffer(copy, &source, &region, false);
  SDL_EndGPUCopyPass(copy);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  return true;
}

}  // namespace

/* -- buffers ------------------------------------------------------------- */

uint32_t WebGLContext::create_buffer() {
  auto record = std::make_unique<WebGLBuffer>();
  record->id = next_object_id_++;
  const uint32_t id = record->id;
  buffers_[id] = std::move(record);
  return id;
}

void WebGLContext::delete_buffer(uint32_t id) {
  auto found = buffers_.find(id);
  if (found == buffers_.end()) return;
  if (bound_array_ == id) bound_array_ = 0;
  if (bound_element_ == id) bound_element_ = 0;
  for (WebGLAttribState &attrib : attribs_) {
    if (attrib.buffer == id) attrib.buffer = 0;
  }
  if (found->second->gpu != nullptr && guidev::g_device != nullptr) {
    submit(); SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUBuffer(guidev::g_device, found->second->gpu);
  }
  buffers_.erase(found);
}

void WebGLContext::bind_buffer(int target, uint32_t id) {
  if (id != 0 && buffers_.find(id) == buffers_.end()) return;
  if (target == GL_ARRAY_BUFFER) bound_array_ = id;
  if (target == GL_ELEMENT_ARRAY_BUFFER) bound_element_ = id;
  if (target == GL_COPY_READ_BUFFER) copy_read_buffer_ = id;
  if (target == GL_COPY_WRITE_BUFFER) copy_write_buffer_ = id;
}

uint32_t WebGLContext::buffer_for_target(int target) const {
  if (target == GL_ELEMENT_ARRAY_BUFFER) return bound_element_;
  if (target == GL_ARRAY_BUFFER) return bound_array_;
  if (target == GL_COPY_READ_BUFFER) return copy_read_buffer_;
  if (target == GL_COPY_WRITE_BUFFER) return copy_write_buffer_;
  return 0;
}

bool WebGLContext::copy_buffer_sub_data(int read_target, int write_target, uint32_t read_offset,
                                        uint32_t write_offset, uint32_t size) {
  WebGLBuffer *read_buffer = buffer(buffer_for_target(read_target));
  WebGLBuffer *write_buffer = buffer(buffer_for_target(write_target));
  if (read_buffer == nullptr || write_buffer == nullptr ||
      read_offset + size > read_buffer->data.size() ||
      write_offset + size > write_buffer->data.size()) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  if (size > 0) {
    memmove(write_buffer->data.data() + write_offset,
            read_buffer->data.data() + read_offset, size);
  }
  write_buffer->dirty = true;
  return ensure_buffer_gpu(write_buffer) != nullptr || size == 0;
}

bool WebGLContext::get_buffer_sub_data(int target, uint32_t offset, uint8_t *out,
                                       uint32_t size) {
  WebGLBuffer *record = buffer(buffer_for_target(target));
  if (record == nullptr || offset + size > record->data.size()) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  if (size > 0) memcpy(out, record->data.data() + offset, size);
  return true;
}

SDL_GPUBuffer *WebGLContext::ensure_buffer_gpu(WebGLBuffer *buffer) {
  if (buffer == nullptr || guidev::g_device == nullptr) return nullptr;
  if (buffer->data.empty()) return nullptr;
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return nullptr;
  const uint32_t size = (uint32_t)buffer->data.size();
  if (buffer->gpu == nullptr || buffer->capacity < size) {
    if (buffer->gpu != nullptr) {
      submit(); SDL_WaitForGPUIdle(guidev::g_device);
      SDL_ReleaseGPUBuffer(guidev::g_device, buffer->gpu);
    }
    SDL_GPUBufferCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_INDEX;
    info.size = size;
    buffer->gpu = SDL_CreateGPUBuffer(guidev::g_device, &info);
    buffer->capacity = size;
    if (buffer->gpu == nullptr) {
      set_error(GL_OUT_OF_MEMORY);
      return nullptr;
    }
  }
  if (buffer->dirty) {
    /* Recreating the buffer above submitted the pending command buffer. */
    ensure_command();
    if (cmd_ == nullptr) return nullptr;
    if (!upload_transfer(guidev::g_device, cmd_, buffer->gpu, buffer->data.data(), size)) {
      set_error(GL_OUT_OF_MEMORY);
      return nullptr;
    }
    buffer->dirty = false;
  }
  return buffer->gpu;
}

bool WebGLContext::buffer_data(uint32_t id, const uint8_t *bytes, uint32_t size, int usage) {
  (void)usage;
  WebGLBuffer *record = buffer(id);
  if (record == nullptr) {
    set_error(GL_INVALID_OPERATION);
    return false;
  }
  record->data.assign(size, 0);
  if (bytes != nullptr && size > 0) memcpy(record->data.data(), bytes, size);
  record->dirty = true;
  return ensure_buffer_gpu(record) != nullptr || size == 0;
}

bool WebGLContext::buffer_sub_data(uint32_t id, uint32_t offset, const uint8_t *bytes,
                                   uint32_t size) {
  WebGLBuffer *record = buffer(id);
  if (record == nullptr || offset > record->data.size() ||
      offset + size > record->data.size()) {
    set_error(GL_INVALID_VALUE);
    return false;
  }
  memcpy(record->data.data() + offset, bytes, size);
  record->dirty = true;
  return ensure_buffer_gpu(record) != nullptr;
}

uint32_t WebGLContext::buffer_size(uint32_t id) {
  WebGLBuffer *record = buffer(id);
  return record != nullptr ? (uint32_t)record->data.size() : 0;
}

/* -- vertex attributes --------------------------------------------------- */

void WebGLContext::enable_attrib(int index, bool enabled) {
  if (index < 0 || index >= kWebGLMaxAttribs) {
    set_error(GL_INVALID_VALUE);
    return;
  }
  attribs_[index].enabled = enabled;
}

bool WebGLContext::attrib_enabled(int index) const {
  return index >= 0 && index < kWebGLMaxAttribs && attribs_[index].enabled;
}

void WebGLContext::attrib_pointer(int index, int size, int type, bool normalized, int stride,
                                  int offset) {
  if (index < 0 || index >= kWebGLMaxAttribs) {
    set_error(GL_INVALID_VALUE);
    return;
  }
  WebGLAttribState &attrib = attribs_[index];
  attrib.buffer = bound_array_;
  attrib.size = size;
  attrib.type = type;
  attrib.normalized = normalized;
  attrib.integer = false;
  attrib.stride = stride > 0 ? stride : 0;
  attrib.offset = offset;
}

void WebGLContext::attrib_ipointer(int index, int size, int type, int stride, int offset) {
  if (index < 0 || index >= kWebGLMaxAttribs) {
    set_error(GL_INVALID_VALUE);
    return;
  }
  WebGLAttribState &attrib = attribs_[index];
  attrib.buffer = bound_array_;
  attrib.size = size;
  attrib.type = type;
  attrib.normalized = false;
  attrib.integer = true;
  attrib.stride = stride > 0 ? stride : 0;
  attrib.offset = offset;
}

void WebGLContext::attrib_divisor(int index, int divisor) {
  if (index < 0 || index >= kWebGLMaxAttribs) {
    set_error(GL_INVALID_VALUE);
    return;
  }
  /* SDL 3.2's `instance_step_rate` is reserved and must be 0, so only the two
   * rates WebGL programs use — per vertex (0) and per instance (1) — map onto
   * the backend. A larger divisor is clamped. */
  attribs_[index].divisor = divisor > 0 ? 1 : 0;
}

void WebGLContext::attrib_value(int index, int components, const float *values) {
  if (index < 0 || index >= kWebGLMaxAttribs) return;
  for (int i = 0; i < 4; i++) attribs_[index].current[i] = i < components ? values[i] : 0.0f;
  if (components < 4) attribs_[index].current[3] = 1.0f;
}

void WebGLContext::attrib_state(int index, int *size, int *type, int *stride, bool *normalized,
                                float current[4]) const {
  if (index < 0 || index >= kWebGLMaxAttribs) return;
  const WebGLAttribState &attrib = attribs_[index];
  *size = attrib.size;
  *type = attrib.type;
  *stride = attrib.stride;
  *normalized = attrib.normalized;
  for (int i = 0; i < 4; i++) current[i] = attrib.current[i];
}

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
  if (guidev::g_device != nullptr) {
    if (found->second->gpu != nullptr) {
      submit(); SDL_WaitForGPUIdle(guidev::g_device);
      SDL_ReleaseGPUTexture(guidev::g_device, found->second->gpu);
    }
    if (found->second->sampler != nullptr) {
      SDL_ReleaseGPUSampler(guidev::g_device, found->second->sampler);
    }
  }
  textures_.erase(found);
}

void WebGLContext::bind_texture(int target, uint32_t id) {
  if (id != 0 && textures_.find(id) == textures_.end()) return;
  if (target != GL_TEXTURE_2D) {
    set_error(GL_INVALID_ENUM);
    return;
  }
  bound_textures_[active_unit_] = id;
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
  else return true;  // other parameters are accepted and ignored
  if (record->sampler != nullptr && guidev::g_device != nullptr) {
    SDL_ReleaseGPUSampler(guidev::g_device, record->sampler);
    record->sampler = nullptr;
  }
  return true;
}

int WebGLContext::tex_parameter_value(uint32_t id, int pname) {
  WebGLTexture *record = texture(id);
  if (record == nullptr) return 0;
  if (pname == GL_TEXTURE_MIN_FILTER) return record->min_filter;
  if (pname == GL_TEXTURE_MAG_FILTER) return record->mag_filter;
  if (pname == GL_TEXTURE_WRAP_S) return record->wrap_s;
  if (pname == GL_TEXTURE_WRAP_T) return record->wrap_t;
  return 0;
}

SDL_GPUTexture *WebGLContext::ensure_texture_gpu(WebGLTexture *record, bool as_target) {
  if (record == nullptr || guidev::g_device == nullptr) return nullptr;
  if (record->width <= 0 || record->height <= 0) return record->gpu;
  if (record->gpu != nullptr && (!as_target || record->as_target)) return record->gpu;
  if (record->gpu != nullptr) {
    submit(); SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, record->gpu);
    record->gpu = nullptr;
  }
  record->as_target = as_target;
  record->levels = mip_levels_for(record->width, record->height);
  SDL_GPUTextureCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = record->format;
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  if (as_target) info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  info.width = (Uint32)record->width;
  info.height = (Uint32)record->height;
  info.layer_count_or_depth = 1;
  info.num_levels = (Uint32)record->levels;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  record->gpu = SDL_CreateGPUTexture(guidev::g_device, &info);
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
  record->sampler = SDL_CreateGPUSampler(guidev::g_device, &info);
  return record->sampler;
}

int WebGLContext::texture_width(uint32_t id) {
  WebGLTexture *record = texture(id);
  return record != nullptr ? record->width : 0;
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
  if (bound_framebuffer_ == id) bind_framebuffer(0);
  if (found->second->depth != nullptr && found->second->depth_renderbuffer == 0 &&
      guidev::g_device != nullptr) {
    submit(); SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUTexture(guidev::g_device, found->second->depth);
  }
  framebuffers_.erase(found);
}

void WebGLContext::bind_framebuffer(uint32_t id) {
  if (id != 0 && framebuffers_.find(id) == framebuffers_.end()) return;
  if (id != bound_framebuffer_) end_pass();
  bound_framebuffer_ = id;
}

bool WebGLContext::framebuffer_texture(uint32_t framebuffer_id, uint32_t texture_id) {
  WebGLFramebuffer *record_framebuffer = framebuffer(framebuffer_id);
  WebGLTexture *record = texture(texture_id);
  if (record_framebuffer == nullptr) return false;
  if (texture_id != 0 && record == nullptr) return false;
  record_framebuffer->color_texture = texture_id;
  if (record != nullptr) {
    record_framebuffer->width = record->width;
    record_framebuffer->height = record->height;
    ensure_texture_gpu(record, true);
  }
  return true;
}

bool WebGLContext::framebuffer_status(uint32_t id) {
  WebGLFramebuffer *framebuffer_ = framebuffer(id);
  if (framebuffer_ == nullptr) return false;
  if (framebuffer_->color_texture == 0) {
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
  info.format = stencil ? SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT
                        : SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
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
