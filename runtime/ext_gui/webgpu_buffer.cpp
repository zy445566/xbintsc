/*
 * xbintsc GUI engine — WebGPU buffer, texture, texture-view and sampler
 * instance methods (creation lives in `webgpu_device.cpp`).
 */

#include <cstring>

#include "webgpu_bindings.h"

namespace xtgui {
namespace webgpu {
namespace {

xt_value label_of(Object &object) { return xt_string_from_cstr(object.label.c_str()); }

/* -- buffer --------------------------------------------------------------- */

xt_value buffer_size(Object &object) { return xt_number((double)static_cast<Buffer &>(object).size); }

xt_value buffer_usage(Object &object) { return xt_number((double)static_cast<Buffer &>(object).usage); }

xt_value buffer_map_state(Object &object) {
  static const char *const kNames[] = {"unmapped", "pending", "mapped"};
  const int state = static_cast<Buffer &>(object).map_state;
  return xt_string_from_cstr(kNames[state >= 0 && state <= 2 ? state : 0]);
}

bool upload_buffer(Buffer &buffer, uint64_t offset, const uint8_t *data, uint64_t size) {
  if (!buffer.has_gpu || size == 0) return true;
  SDL_GPUTransferBufferCreateInfo info = {};
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  info.size = (Uint32)size;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpu_device(), &info);
  if (transfer == nullptr) return false;
  void *mapped = SDL_MapGPUTransferBuffer(gpu_device(), transfer, false);
  if (mapped != nullptr) {
    memcpy(mapped, data, (size_t)size);
    SDL_UnmapGPUTransferBuffer(gpu_device(), transfer);
  }
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gpu_device());
  SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTransferBufferLocation source = {};
  source.transfer_buffer = transfer;
  source.offset = 0;
  SDL_GPUBufferRegion destination = {};
  destination.buffer = buffer.gpu;
  destination.offset = (Uint32)offset;
  destination.size = (Uint32)size;
  SDL_UploadToGPUBuffer(pass, &source, &destination, true);
  SDL_EndGPUCopyPass(pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(gpu_device(), transfer);
  return true;
}

xt_value buffer_map_async(Object &object, int argc, xt_value *argv) {
  Buffer &buffer = static_cast<Buffer &>(object);
  std::string error;
  std::shared_ptr<Device> device = device_for(object, &error);
  if (device == nullptr) return xt_promise_reject(make_error(ErrorKind::Validation, error));
  const std::string mode = arg_string(argc, argv, 0);
  const uint64_t offset = arg_u64(argc, argv, 1);
  uint64_t size = arg_u64(argc, argv, 2);
  if (size == 0) size = buffer.size > offset ? buffer.size - offset : 0;
  if (offset + size > buffer.size) {
    return xt_promise_reject(make_error(ErrorKind::Validation, "mapAsync range is out of bounds"));
  }
  if (buffer.map_state != 0) {
    return xt_promise_reject(make_error(ErrorKind::Validation, "buffer is already mapped"));
  }
  if ((mode == "READ" && (buffer.usage & 1) == 0) ||
      (mode == "WRITE" && (buffer.usage & 2) == 0)) {
    return xt_promise_reject(
        make_error(ErrorKind::Validation, "mapAsync mode does not match the buffer usage"));
  }
  buffer.map_mode = mode == "WRITE" ? 2 : 1;
  buffer.map_state = 1;
  if (!buffer.has_shadow) buffer.shadow.assign((size_t)buffer.size, 0);
  if (!buffer.has_gpu) {
    buffer.map_state = 2;
    return resolved(XT_UNDEFINED);
  }
  if (buffer.map_mode == 2) {
    /* Nothing to read: the upload happens on unmap. */
    buffer.map_state = 2;
    return resolved(XT_UNDEFINED);
  }
  SDL_GPUTransferBufferCreateInfo info = {};
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  info.size = (Uint32)size;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpu_device(), &info);
  if (transfer == nullptr) {
    buffer.map_state = 0;
    return xt_promise_reject(make_error(ErrorKind::Validation, "mapAsync readback failed"));
  }
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gpu_device());
  SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUBufferRegion region = {};
  region.buffer = buffer.gpu;
  region.offset = (Uint32)offset;
  region.size = (Uint32)size;
  SDL_GPUTransferBufferLocation location = {};
  location.transfer_buffer = transfer;
  location.offset = 0;
  SDL_DownloadFromGPUBuffer(pass, &region, &location);
  SDL_EndGPUCopyPass(pass);
  SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (fence != nullptr) {
    SDL_GPUFence *fences[1] = {fence};
    SDL_WaitForGPUFences(gpu_device(), true, fences, 1);
    SDL_ReleaseGPUFence(gpu_device(), fence);
  }
  void *mapped = SDL_MapGPUTransferBuffer(gpu_device(), transfer, false);
  if (mapped != nullptr) {
    memcpy(buffer.shadow.data() + offset, mapped, (size_t)size);
    SDL_UnmapGPUTransferBuffer(gpu_device(), transfer);
  }
  SDL_ReleaseGPUTransferBuffer(gpu_device(), transfer);
  buffer.map_state = 2;
  return resolved(XT_UNDEFINED);
}

xt_value buffer_get_mapped_range(Object &object, int argc, xt_value *argv) {
  Buffer &buffer = static_cast<Buffer &>(object);
  if (buffer.map_state != 2) {
    std::string error;
    std::shared_ptr<Device> device = device_for(object, &error);
    if (device != nullptr) {
      validation(*device, "getMappedRange: the buffer is not mapped");
    }
    return XT_UNDEFINED;
  }
  const uint64_t offset = arg_u64(argc, argv, 0);
  uint64_t size = arg_u64(argc, argv, 1);
  if (size == 0) size = buffer.shadow.size() > offset ? buffer.shadow.size() - offset : 0;
  if (offset + size > buffer.shadow.size()) return XT_UNDEFINED;
  xt_value array = xt_array_buffer_alloc((uint32_t)size);
  xt_array_buffer_copy_bytes_in(array, 0, buffer.shadow.data() + offset, (uint32_t)size);
  buffer.mapped_array = array;
  buffer.mapped_offset = offset;
  buffer.mapped_size = size;
  xt_gc_add_root(&buffer.mapped_array);
  return array;
}

xt_value buffer_unmap(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  Buffer &buffer = static_cast<Buffer &>(object);
  if (buffer.map_state != 2) return XT_UNDEFINED;
  if (buffer.map_mode == 2 && buffer.map_state == 2) {
    /* A mapped WRITE range is a copy of the shadow; take the edits back. */
    if (XT_IS_OBJECT(buffer.mapped_array) && buffer.mapped_size > 0 &&
        buffer.mapped_offset + buffer.mapped_size <= buffer.shadow.size()) {
      xt_array_buffer_copy_bytes_out(buffer.mapped_array, 0,
                                     buffer.shadow.data() + buffer.mapped_offset,
                                     (uint32_t)buffer.mapped_size);
      upload_buffer(buffer, buffer.mapped_offset, buffer.shadow.data() + buffer.mapped_offset,
                    buffer.mapped_size);
    } else if (!buffer.shadow.empty()) {
      upload_buffer(buffer, 0, buffer.shadow.data(), buffer.shadow.size());
    }
  }
  buffer.map_state = 0;
  buffer.map_mode = 0;
  buffer.mapped_array = XT_UNDEFINED;
  return XT_UNDEFINED;
}

xt_value buffer_destroy(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  Buffer &buffer = static_cast<Buffer &>(object);
  if (buffer.destroyed) return XT_UNDEFINED;
  if (buffer.gpu != nullptr) SDL_ReleaseGPUBuffer(gpu_device(), buffer.gpu);
  buffer.gpu = nullptr;
  buffer.has_gpu = false;
  buffer.destroyed = true;
  buffer.map_state = 0;
  return XT_UNDEFINED;
}

/* -- texture -------------------------------------------------------------- */

xt_value texture_width(Object &object) {
  return xt_number((double)static_cast<Texture &>(object).width);
}

xt_value texture_height(Object &object) {
  return xt_number((double)static_cast<Texture &>(object).height);
}

xt_value texture_depth(Object &object) {
  return xt_number((double)static_cast<Texture &>(object).depth);
}

xt_value texture_mips(Object &object) {
  return xt_number((double)static_cast<Texture &>(object).mips);
}

xt_value texture_samples(Object &object) {
  return xt_number((double)static_cast<Texture &>(object).samples);
}

xt_value texture_dimension(Object &object) {
  return xt_string_from_cstr(static_cast<Texture &>(object).dimension.c_str());
}

xt_value texture_format(Object &object) {
  return xt_string_from_cstr(Convert::texture_format_name(static_cast<Texture &>(object).format));
}

xt_value texture_usage(Object &object) {
  return xt_number((double)static_cast<Texture &>(object).usage);
}

xt_value texture_create_view(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  Texture &texture = static_cast<Texture &>(object);
  if (texture.destroyed) {
    std::string error;
    std::shared_ptr<Device> device = device_for(object, &error);
    if (device != nullptr) validation(*device, "createView: the texture was destroyed");
    return XT_UNDEFINED;
  }
  auto view = std::make_shared<TextureView>();
  view->kind = Kind::TextureView;
  view->device_id = texture.device_id;
  view->texture = texture.id;
  return handle_of(add_object(view));
}

xt_value texture_destroy(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  Texture &texture = static_cast<Texture &>(object);
  if (texture.destroyed) return XT_UNDEFINED;
  if (texture.gpu != nullptr && !texture.external) {
    SDL_ReleaseGPUTexture(gpu_device(), texture.gpu);
  }
  texture.gpu = nullptr;
  texture.destroyed = true;
  return XT_UNDEFINED;
}

/* -- registration --------------------------------------------------------- */

}  // namespace

void register_buffer_methods() {
  add_getter(Kind::Buffer, "size", buffer_size);
  add_getter(Kind::Buffer, "usage", buffer_usage);
  add_getter(Kind::Buffer, "mapState", buffer_map_state);
  add_getter(Kind::Buffer, "label", label_of);
  add_method(Kind::Buffer, "mapAsync", buffer_map_async);
  add_method(Kind::Buffer, "getMappedRange", buffer_get_mapped_range);
  add_method(Kind::Buffer, "unmap", buffer_unmap);
  add_method(Kind::Buffer, "destroy", buffer_destroy);

  add_getter(Kind::Texture, "width", texture_width);
  add_getter(Kind::Texture, "height", texture_height);
  add_getter(Kind::Texture, "depthOrArrayLayers", texture_depth);
  add_getter(Kind::Texture, "mipLevelCount", texture_mips);
  add_getter(Kind::Texture, "sampleCount", texture_samples);
  add_getter(Kind::Texture, "dimension", texture_dimension);
  add_getter(Kind::Texture, "format", texture_format);
  add_getter(Kind::Texture, "usage", texture_usage);
  add_getter(Kind::Texture, "label", label_of);
  add_method(Kind::Texture, "createView", texture_create_view);
  add_method(Kind::Texture, "destroy", texture_destroy);

  add_getter(Kind::TextureView, "label", label_of);
  add_getter(Kind::Sampler, "label", label_of);
  add_getter(Kind::QuerySet, "label", label_of);
  add_getter(Kind::QuerySet, "type", [](Object &object) -> xt_value {
    return xt_string_from_cstr(static_cast<QuerySet &>(object).type == 1 ? "timestamp"
                                                                          : "occlusion");
  });
  add_getter(Kind::QuerySet, "count", [](Object &object) -> xt_value {
    return xt_number((double)static_cast<QuerySet &>(object).count);
  });
  add_method(Kind::QuerySet, "destroy", [](Object &object, int argc, xt_value *argv) -> xt_value {
    (void)argc;
    (void)argv;
    (void)object;
    return XT_UNDEFINED;
  });
  add_getter(Kind::RenderBundle, "label", label_of);
}

}  // namespace webgpu
}  // namespace xtgui
