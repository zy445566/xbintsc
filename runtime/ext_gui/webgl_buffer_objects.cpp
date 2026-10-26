/*
 * xbintsc GUI engine — WebGL buffer objects and vertex-attribute state.
 *
 * Uploads are recorded immediately: a copy pass cannot run inside a render
 * pass, so a buffer update ends the open pass first. The next draw starts a
 * new pass with `LOAD`, so the framebuffer keeps its contents.
 */

#include "webgl.h"

#include <algorithm>
#include <cstring>

#include "gui_internal.h"

namespace xtgui {
namespace {

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
  if (target == GL_UNIFORM_BUFFER) bound_uniform_ = id;
  if (target == GL_TRANSFORM_FEEDBACK_BUFFER) bound_transform_feedback_ = id;
}

uint32_t WebGLContext::buffer_for_target(int target) const {
  if (target == GL_ELEMENT_ARRAY_BUFFER) return bound_element_;
  if (target == GL_ARRAY_BUFFER) return bound_array_;
  if (target == GL_COPY_READ_BUFFER) return copy_read_buffer_;
  if (target == GL_COPY_WRITE_BUFFER) return copy_write_buffer_;
  if (target == GL_UNIFORM_BUFFER) return bound_uniform_;
  if (target == GL_TRANSFORM_FEEDBACK_BUFFER) return bound_transform_feedback_;
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
  WebGLBuffer *record = buffer(id);
  if (record == nullptr) {
    set_error(GL_INVALID_OPERATION);
    return false;
  }
  if (usage == GL_STREAM_DRAW || usage == GL_STATIC_DRAW || usage == GL_DYNAMIC_DRAW) {
    record->usage = usage;
  } else if (usage != 0) {
    set_error(GL_INVALID_ENUM);
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

}  // namespace xtgui
