/*
 * xbintsc GUI engine — WebGPU queue operations.
 */

#include <cstring>

#include "webgpu_bindings.h"

namespace xtgui {
namespace webgpu {
namespace {

xt_value queue_label(Object &object) {
  return xt_string_from_cstr(object.label.c_str());
}

xt_value queue_submit(Object &object, int argc, xt_value *argv) {
  (void)object;
  xt_value list = arg_object(argc, argv, 0);
  if (!XT_IS_ARRAY(list)) {
    throw_type_error("submit needs an array of command buffers");
    return XT_UNDEFINED;
  }
  const uint32_t count = (uint32_t)xt_to_number(xt_array_length(list));
  for (uint32_t i = 0; i < count; i++) {
    xt_value item = xt_array_get(list, xt_number((double)i));
    const uint32_t id = (uint32_t)xt_to_number(xt_object_get_cstr(item, "__xt_webgpu_id"));
    std::shared_ptr<Object> command = get_object(id);
    if (command == nullptr) continue;
    if (command->kind == Kind::CommandBuffer) {
      auto *buffer = static_cast<CommandBuffer *>(command.get());
      if (buffer->cmd != nullptr) {
        SDL_SubmitGPUCommandBuffer(buffer->cmd);
        buffer->cmd = nullptr;
      }
      continue;
    }
    if (command->kind != Kind::CommandEncoder) continue;
    auto *encoder = static_cast<CommandEncoder *>(command.get());
    if (encoder->finished || encoder->cmd == nullptr) continue;
    encoder->finished = true;
    SDL_SubmitGPUCommandBuffer(encoder->cmd);
    encoder->cmd = nullptr;
  }
  return XT_UNDEFINED;
}

xt_value queue_write_buffer(Object &object, int argc, xt_value *argv) {
  xt_value buffer_value = arg_object(argc, argv, 0);
  const uint32_t buffer_id = (uint32_t)xt_to_number(
      xt_object_get_cstr(buffer_value, "__xt_webgpu_id"));
  std::shared_ptr<Object> buffer_object = get_object(buffer_id, Kind::Buffer);
  if (buffer_object == nullptr) {
    std::shared_ptr<Device> device = device_for(object, nullptr);
    if (device != nullptr) return validation(*device, "writeBuffer: invalid buffer");
    return XT_UNDEFINED;
  }
  Buffer &buffer = static_cast<Buffer &>(*buffer_object);
  const uint64_t offset = arg_u64(argc, argv, 1);
  std::vector<uint8_t> data;
  if (!read_bytes(xt_arg(argc, argv, 2), &data)) {
    throw_type_error("writeBuffer needs an ArrayBuffer or typed array");
    return XT_UNDEFINED;
  }
  const uint64_t data_offset = arg_u64(argc, argv, 3);
  uint64_t size = arg_u64(argc, argv, 4);
  if (size == 0) size = data.size() > data_offset ? data.size() - data_offset : 0;
  if (data_offset + size > data.size() || offset + size > buffer.size) {
    std::shared_ptr<Device> device = device_for(object, nullptr);
    if (device != nullptr) {
      return validation(*device, "writeBuffer: the write range is out of bounds");
    }
    return XT_UNDEFINED;
  }
  if (buffer.has_shadow) {
    memcpy(buffer.shadow.data() + offset, data.data() + data_offset, (size_t)size);
  }
  if (buffer.has_gpu) {
    /* Upload now; WebGPU's write ordering is equivalent for our single device. */
    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = (Uint32)size;
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpu_device(), &info);
    if (transfer != nullptr) {
      void *mapped = SDL_MapGPUTransferBuffer(gpu_device(), transfer, false);
      if (mapped != nullptr) {
        memcpy(mapped, data.data() + data_offset, (size_t)size);
        SDL_UnmapGPUTransferBuffer(gpu_device(), transfer);
        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gpu_device());
        SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation source = {};
        source.transfer_buffer = transfer;
        source.offset = 0;
        SDL_GPUBufferRegion region = {};
        region.buffer = buffer.gpu;
        region.offset = (Uint32)offset;
        region.size = (Uint32)size;
        SDL_UploadToGPUBuffer(pass, &source, &region, true);
        SDL_EndGPUCopyPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
      }
      SDL_ReleaseGPUTransferBuffer(gpu_device(), transfer);
    }
  }
  return XT_UNDEFINED;
}

xt_value queue_write_texture(Object &object, int argc, xt_value *argv) {
  xt_value destination = arg_object(argc, argv, 0);
  std::vector<uint8_t> data;
  if (!read_bytes(xt_arg(argc, argv, 1), &data)) {
    throw_type_error("writeTexture needs an ArrayBuffer or typed array");
    return XT_UNDEFINED;
  }
  xt_value layout = arg_object(argc, argv, 2);
  xt_value size_value = arg_object(argc, argv, 3);
  if (!XT_IS_OBJECT(destination) || !XT_IS_OBJECT(size_value)) {
    throw_type_error("writeTexture needs a destination and a size");
    return XT_UNDEFINED;
  }
  const uint32_t texture_id = (uint32_t)xt_to_number(
      xt_object_get_cstr(prop_object(destination, "texture"), "__xt_webgpu_id"));
  std::shared_ptr<Object> texture_object = get_object(texture_id, Kind::Texture);
  if (texture_object == nullptr) {
    std::shared_ptr<Device> device = device_for(object, nullptr);
    if (device != nullptr) return validation(*device, "writeTexture: invalid texture");
    return XT_UNDEFINED;
  }
  Texture &texture = static_cast<Texture &>(*texture_object);
  const uint32_t width = (uint32_t)prop_u64(size_value, "width", 0);
  const uint32_t height = (uint32_t)prop_u64(size_value, "height", 1);
  const uint32_t depth = (uint32_t)prop_u64(size_value, "depthOrArrayLayers", 1);
  xt_value origin = prop_object(destination, "origin");
  const uint32_t x = (uint32_t)prop_u64(origin, "x", 0);
  const uint32_t y = (uint32_t)prop_u64(origin, "y", 0);
  const uint32_t z = (uint32_t)prop_u64(origin, "z", 0);
  const uint32_t mip = (uint32_t)prop_u64(destination, "mipLevel", 0);
  const uint32_t bytes_per_row = (uint32_t)prop_u64(layout, "bytesPerRow", width * 4);
  const uint64_t data_offset = prop_u64(layout, "offset", 0);
  const uint32_t bytes_per_pixel = Convert::bytes_per_pixel(texture.format);
  const uint64_t row_bytes = (uint64_t)width * bytes_per_pixel;
  if (row_bytes > bytes_per_row) {
    std::shared_ptr<Device> device = device_for(object, nullptr);
    if (device != nullptr) return validation(*device, "writeTexture: bytesPerRow is too small");
    return XT_UNDEFINED;
  }
  const uint64_t needed = data_offset + (uint64_t)bytes_per_row * (height > 1 ? height - 1 : 0) +
                          row_bytes * (uint64_t)depth * (height > 1 ? 1 : height);
  if (needed > data.size() + row_bytes) {
    std::shared_ptr<Device> device = device_for(object, nullptr);
    if (device != nullptr) return validation(*device, "writeTexture: the data is too short");
    return XT_UNDEFINED;
  }
  SDL_GPUTransferBufferCreateInfo info = {};
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  info.size = (Uint32)data.size();
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpu_device(), &info);
  if (transfer == nullptr) {
    std::shared_ptr<Device> device = device_for(object, nullptr);
    if (device != nullptr) return validation(*device, "writeTexture: transfer buffer failed");
    return XT_UNDEFINED;
  }
  void *mapped = SDL_MapGPUTransferBuffer(gpu_device(), transfer, false);
  if (mapped != nullptr) {
    memcpy(mapped, data.data(), data.size());
    SDL_UnmapGPUTransferBuffer(gpu_device(), transfer);
  }
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gpu_device());
  SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureTransferInfo source = {};
  source.transfer_buffer = transfer;
  source.offset = (Uint32)data_offset;
  source.pixels_per_row = bytes_per_row / bytes_per_pixel;
  source.rows_per_layer = height;
  SDL_GPUTextureRegion region = {};
  region.texture = texture.gpu;
  region.mip_level = mip;
  region.layer = z;
  region.x = x;
  region.y = y;
  region.z = 0;
  region.w = width;
  region.h = height;
  region.d = depth;
  SDL_UploadToGPUTexture(pass, &source, &region, true);
  SDL_EndGPUCopyPass(pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(gpu_device(), transfer);
  return XT_UNDEFINED;
}

xt_value queue_copy_external_image(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  std::shared_ptr<Device> device = device_for(object, nullptr);
  if (device != nullptr) {
    return validation(*device,
                      "copyExternalImageToTexture: DOM image sources are not supported by the WebGPU backend");
  }
  return XT_UNDEFINED;
}

xt_value queue_on_submitted_work_done(Object &object, int argc, xt_value *argv) {
  (void)object;
  (void)argc;
  (void)argv;
  return resolved(XT_UNDEFINED);
}

}  // namespace

void register_queue_methods() {
  add_getter(Kind::Queue, "label", queue_label);
  add_method(Kind::Queue, "submit", queue_submit);
  add_method(Kind::Queue, "writeBuffer", queue_write_buffer);
  add_method(Kind::Queue, "writeTexture", queue_write_texture);
  add_method(Kind::Queue, "copyExternalImageToTexture", queue_copy_external_image);
  add_method(Kind::Queue, "onSubmittedWorkDone", queue_on_submitted_work_done);
}

}  // namespace webgpu
}  // namespace xtgui
