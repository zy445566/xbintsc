/*
 * xbintsc GUI engine — WebGPU command encoding.
 */

#include <cstring>
#include <vector>

#include "webgpu_bindings.h"
#include "webgpu_pass.h"

namespace xtgui {
namespace webgpu {
namespace {

SDL_GPUCommandBuffer *encoder_cmd(CommandEncoder &encoder) {
  if (encoder.cmd == nullptr && !encoder.finished) {
    encoder.cmd = SDL_AcquireGPUCommandBuffer(gpu_device());
  }
  return encoder.cmd;
}

/* Flush the encoder so an immediate readback/copy can run in order. */
void flush_encoder(CommandEncoder &encoder) {
  if (encoder.cmd == nullptr) return;
  SDL_SubmitGPUCommandBuffer(encoder.cmd);
  encoder.cmd = nullptr;
}

xt_value encoder_label(Object &object) { return xt_string_from_cstr(object.label.c_str()); }

xt_value value_id(xt_value value) {
  if (!XT_IS_OBJECT(value)) return 0;
  return (uint32_t)xt_to_number(xt_object_get_cstr(value, "__xt_webgpu_id"));
}

Buffer *buffer_at(xt_value value) {
  std::shared_ptr<Object> object = get_object(value_id(value), Kind::Buffer);
  return object == nullptr ? nullptr : static_cast<Buffer *>(object.get());
}

Texture *texture_at(xt_value value) {
  std::shared_ptr<Object> object = get_object(value_id(value), Kind::Texture);
  return object == nullptr ? nullptr : static_cast<Texture *>(object.get());
}

xt_value view_id_of(xt_value view) {
  return xt_number((double)value_id(view));
}

bool read_texture_rows(Texture &texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t z,
                       uint32_t width, uint32_t height, uint32_t depth, uint32_t bytes_per_row,
                       std::vector<uint8_t> *out) {
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gpu_device());
  if (cmd == nullptr) return false;
  const uint32_t bytes_per_pixel = Convert::bytes_per_pixel(texture.format);
  const uint64_t row_bytes = (uint64_t)width * bytes_per_pixel;
  const uint64_t rows_per_image = height;
  const uint64_t total = ((row_bytes + bytes_per_row - 1) / bytes_per_row) * bytes_per_row *
                         rows_per_image * depth;
  SDL_GPUTransferBufferCreateInfo info = {};
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  info.size = (Uint32)total;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpu_device(), &info);
  if (transfer == nullptr) return false;
  SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
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
  SDL_GPUTextureTransferInfo destination = {};
  destination.transfer_buffer = transfer;
  destination.offset = 0;
  destination.pixels_per_row = bytes_per_row / bytes_per_pixel;
  destination.rows_per_layer = (Uint32)rows_per_image;
  SDL_DownloadFromGPUTexture(pass, &region, &destination);
  SDL_EndGPUCopyPass(pass);
  SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (fence != nullptr) {
    SDL_GPUFence *fences[1] = {fence};
    SDL_WaitForGPUFences(gpu_device(), true, fences, 1);
    SDL_ReleaseGPUFence(gpu_device(), fence);
  }
  void *mapped = SDL_MapGPUTransferBuffer(gpu_device(), transfer, false);
  if (mapped != nullptr) {
    out->assign((const uint8_t *)mapped, (const uint8_t *)mapped + total);
    SDL_UnmapGPUTransferBuffer(gpu_device(), transfer);
  }
  SDL_ReleaseGPUTransferBuffer(gpu_device(), transfer);
  return mapped != nullptr;
}

bool upload_texture_rows(Texture &texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t z,
                         uint32_t width, uint32_t height, uint32_t depth, uint32_t bytes_per_row,
                         const std::vector<uint8_t> &data, uint64_t offset) {
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gpu_device());
  if (cmd == nullptr) return false;
  SDL_GPUTransferBufferCreateInfo info = {};
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  info.size = (Uint32)data.size();
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpu_device(), &info);
  if (transfer == nullptr) return false;
  void *mapped = SDL_MapGPUTransferBuffer(gpu_device(), transfer, false);
  if (mapped != nullptr) {
    memcpy(mapped, data.data(), data.size());
    SDL_UnmapGPUTransferBuffer(gpu_device(), transfer);
  }
  const uint32_t bytes_per_pixel = Convert::bytes_per_pixel(texture.format);
  SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureTransferInfo source = {};
  source.transfer_buffer = transfer;
  source.offset = (Uint32)offset;
  source.pixels_per_row = bytes_per_row / (bytes_per_pixel == 0 ? 1 : bytes_per_pixel);
  source.rows_per_layer = height;
  SDL_GPUTextureRegion region = {};
  region.texture = texture.gpu;
  region.mip_level = mip;
  region.layer = z;
  region.x = x;
  region.y = y;
  region.w = width;
  region.h = height;
  region.d = depth;
  SDL_UploadToGPUTexture(pass, &source, &region, true);
  SDL_EndGPUCopyPass(pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(gpu_device(), transfer);
  return true;
}

bool write_buffer_bytes(Buffer &buffer, uint64_t offset, const std::vector<uint8_t> &data) {
  if (buffer.has_shadow && offset + data.size() <= buffer.shadow.size()) {
    memcpy(buffer.shadow.data() + offset, data.data(), data.size());
  }
  if (!buffer.has_gpu || buffer.gpu == nullptr) return true;
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gpu_device());
  SDL_GPUTransferBufferCreateInfo info = {};
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  info.size = (Uint32)data.size();
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpu_device(), &info);
  if (transfer == nullptr) return false;
  void *mapped = SDL_MapGPUTransferBuffer(gpu_device(), transfer, false);
  if (mapped != nullptr) {
    memcpy(mapped, data.data(), data.size());
    SDL_UnmapGPUTransferBuffer(gpu_device(), transfer);
  }
  SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTransferBufferLocation source = {};
  source.transfer_buffer = transfer;
  source.offset = 0;
  SDL_GPUBufferRegion region = {};
  region.buffer = buffer.gpu;
  region.offset = (Uint32)offset;
  region.size = (Uint32)data.size();
  SDL_UploadToGPUBuffer(pass, &source, &region, true);
  SDL_EndGPUCopyPass(pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(gpu_device(), transfer);
  return true;
}

/* -- render pass ---------------------------------------------------------- */

xt_value begin_render_pass(Object &object, int argc, xt_value *argv) {
  CommandEncoder &encoder = static_cast<CommandEncoder &>(object);
  xt_value descriptor = arg_object(argc, argv, 0);
  if (!XT_IS_OBJECT(descriptor)) {
    throw_type_error("beginRenderPass needs a descriptor");
    return XT_UNDEFINED;
  }
  if (encoder.finished) return XT_UNDEFINED;
  std::vector<SDL_GPUColorTargetInfo> targets;
  uint32_t target_width = 0;
  uint32_t target_height = 0;
  xt_value colors = prop_object(descriptor, "colorAttachments");
  if (XT_IS_ARRAY(colors)) {
    const uint32_t count = (uint32_t)xt_to_number(xt_array_length(colors));
    for (uint32_t i = 0; i < count; i++) {
      xt_value item = xt_array_get(colors, xt_number((double)i));
      SDL_GPUColorTargetInfo target = {};
      xt_value view = prop_object(item, "view");
      const uint32_t view_id = (uint32_t)value_id(view);
      std::shared_ptr<Object> view_object = get_object(view_id, Kind::TextureView);
      if (view_object != nullptr) {
        std::shared_ptr<Object> texture_object =
            get_object(static_cast<TextureView *>(view_object.get())->texture, Kind::Texture);
        if (texture_object != nullptr) {
          Texture *native = static_cast<Texture *>(texture_object.get());
          target.texture = native->gpu;
          if (target_width == 0) {
            target_width = native->width;
            target_height = native->height;
          }
        }
      }
      xt_value clear = prop_object(item, "clearValue");
      target.clear_color.r = (float)prop_number(clear, "r", 0.0);
      target.clear_color.g = (float)prop_number(clear, "g", 0.0);
      target.clear_color.b = (float)prop_number(clear, "b", 0.0);
      target.clear_color.a = (float)prop_number(clear, "a", 1.0);
      target.load_op = prop_string(item, "loadOp") == "load" ? SDL_GPU_LOADOP_LOAD
                                                              : SDL_GPU_LOADOP_CLEAR;
      target.store_op = prop_string(item, "storeOp") == "discard" ? SDL_GPU_STOREOP_DONT_CARE
                                                                  : SDL_GPU_STOREOP_STORE;
      xt_value resolve = prop_object(item, "resolveTarget");
      if (XT_IS_OBJECT(resolve)) {
        std::shared_ptr<Object> resolve_view = get_object((uint32_t)value_id(resolve),
                                                          Kind::TextureView);
        if (resolve_view != nullptr) {
          std::shared_ptr<Object> texture_object = get_object(
              static_cast<TextureView *>(resolve_view.get())->texture, Kind::Texture);
          if (texture_object != nullptr) {
            target.resolve_texture = static_cast<Texture *>(texture_object.get())->gpu;
            target.store_op = SDL_GPU_STOREOP_RESOLVE;
          }
        }
      }
      target.cycle = true;
      targets.push_back(target);
    }
  }
  SDL_GPUDepthStencilTargetInfo depth = {};
  bool has_depth = false;
  xt_value depth_value = prop_object(descriptor, "depthStencilAttachment");
  if (XT_IS_OBJECT(depth_value)) {
    xt_value view = prop_object(depth_value, "view");
    std::shared_ptr<Object> view_object = get_object((uint32_t)value_id(view), Kind::TextureView);
    if (view_object != nullptr) {
      std::shared_ptr<Object> texture_object =
          get_object(static_cast<TextureView *>(view_object.get())->texture, Kind::Texture);
      if (texture_object != nullptr) {
        depth.texture = static_cast<Texture *>(texture_object.get())->gpu;
        has_depth = true;
      }
    }
    depth.clear_depth = (float)prop_number(depth_value, "depthClearValue", 1.0);
    depth.load_op = prop_string(depth_value, "depthLoadOp") == "load" ? SDL_GPU_LOADOP_LOAD
                                                                      : SDL_GPU_LOADOP_CLEAR;
    depth.store_op = prop_string(depth_value, "depthStoreOp") == "discard"
                         ? SDL_GPU_STOREOP_DONT_CARE
                         : SDL_GPU_STOREOP_STORE;
    depth.clear_stencil = (Uint8)prop_u64(depth_value, "stencilClearValue", 0);
    depth.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    depth.stencil_store_op = SDL_GPU_STOREOP_STORE;
    depth.cycle = true;
  }
  SDL_GPUCommandBuffer *cmd = encoder_cmd(encoder);
  SDL_GPURenderPass *pass =
      SDL_BeginGPURenderPass(cmd, targets.empty() ? nullptr : targets.data(),
                             (Uint32)targets.size(), has_depth ? &depth : nullptr);
  if (pass == nullptr) {
    std::shared_ptr<Device> device = device_for(encoder, nullptr);
    if (device != nullptr) validation(*device, "beginRenderPass failed");
    return XT_UNDEFINED;
  }
  if (target_width > 0 && target_height > 0) {
    SDL_GPUViewport viewport = {};
    viewport.w = (float)target_width;
    viewport.h = (float)target_height;
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(pass, &viewport);
    SDL_Rect scissor = {};
    scissor.w = (int)target_width;
    scissor.h = (int)target_height;
    SDL_SetGPUScissor(pass, &scissor);
  }
  auto pass_object = std::make_shared<RenderPassEncoder>();
  pass_object->kind = Kind::RenderPassEncoder;
  pass_object->device_id = encoder.device_id;
  pass_object->encoder = encoder.id;
  pass_object->pass = pass;
  return handle_of(add_object(pass_object));
}

/* -- compute pass --------------------------------------------------------- */

xt_value begin_compute_pass(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  CommandEncoder &encoder = static_cast<CommandEncoder &>(object);
  if (encoder.finished) return XT_UNDEFINED;
  auto pass_object = std::make_shared<ComputePassEncoder>();
  pass_object->kind = Kind::ComputePassEncoder;
  pass_object->device_id = encoder.device_id;
  pass_object->encoder = encoder.id;
  return handle_of(add_object(pass_object));
}

/* -- copies --------------------------------------------------------------- */

xt_value copy_buffer_to_buffer(Object &object, int argc, xt_value *argv) {
  CommandEncoder &encoder = static_cast<CommandEncoder &>(object);
  Buffer *source = buffer_at(xt_arg(argc, argv, 0));
  const uint64_t source_offset = arg_u64(argc, argv, 1);
  Buffer *destination = buffer_at(xt_arg(argc, argv, 2));
  const uint64_t destination_offset = arg_u64(argc, argv, 3);
  uint64_t size = arg_u64(argc, argv, 4);
  if (source == nullptr || destination == nullptr || size == 0) return XT_UNDEFINED;
  if (source->has_gpu && destination->has_gpu) {
    SDL_GPUCommandBuffer *cmd = encoder_cmd(encoder);
    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUBufferLocation from = {};
    from.buffer = source->gpu;
    from.offset = (Uint32)source_offset;
    SDL_GPUBufferLocation to = {};
    to.buffer = destination->gpu;
    to.offset = (Uint32)destination_offset;
    SDL_CopyGPUBufferToBuffer(pass, &from, &to, (Uint32)size, true);
    SDL_EndGPUCopyPass(pass);
    return XT_UNDEFINED;
  }
  if (source->has_shadow && destination->has_shadow && source_offset + size <= source->shadow.size() &&
      destination_offset + size <= destination->shadow.size()) {
    memcpy(destination->shadow.data() + destination_offset, source->shadow.data() + source_offset,
           (size_t)size);
  }
  return XT_UNDEFINED;
}

xt_value copy_buffer_to_texture(Object &object, int argc, xt_value *argv) {
  CommandEncoder &encoder = static_cast<CommandEncoder &>(object);
  xt_value source_value = arg_object(argc, argv, 0);
  Buffer *source = buffer_at(prop_object(source_value, "buffer"));
  xt_value destination = arg_object(argc, argv, 1);
  Texture *texture = texture_at(prop_object(destination, "texture"));
  xt_value size = arg_object(argc, argv, 2);
  if (source == nullptr || texture == nullptr) return XT_UNDEFINED;
  const uint64_t source_offset = prop_u64(source_value, "offset", 0);
  const uint32_t width = (uint32_t)prop_u64(size, "width", 0);
  const uint32_t height = (uint32_t)prop_u64(size, "height", 1);
  const uint32_t depth = (uint32_t)prop_u64(size, "depthOrArrayLayers", 1);
  xt_value origin = prop_object(destination, "origin");
  const uint32_t bytes_per_row = (uint32_t)prop_u64(source_value, "bytesPerRow", 0);
  const uint32_t rows_per_image = (uint32_t)prop_u64(source_value, "rowsPerImage", 0);
  const uint32_t row_bytes = width * Convert::bytes_per_pixel(texture->format);
  const uint32_t stride = bytes_per_row != 0 ? bytes_per_row : row_bytes;
  std::vector<uint8_t> data;
  const uint64_t needed = (uint64_t)stride * (rows_per_image != 0 ? rows_per_image : height) * depth;
  if (source->has_shadow && source_offset + needed <= source->shadow.size()) {
    data.assign(source->shadow.begin() + (size_t)source_offset,
                source->shadow.begin() + (size_t)(source_offset + needed));
  } else {
    flush_encoder(encoder);
    std::vector<uint8_t> download;
    if (!read_texture_rows(*texture, 0, 0, 0, 0, 1, 1, 1, 4, &download)) return XT_UNDEFINED;
    data.assign((size_t)needed, 0);
  }
  upload_texture_rows(*texture, (uint32_t)prop_u64(destination, "mipLevel", 0),
                      (uint32_t)prop_u64(origin, "x", 0), (uint32_t)prop_u64(origin, "y", 0),
                      (uint32_t)prop_u64(origin, "z", 0), width, height, depth, stride, data,
                      source_offset);
  return XT_UNDEFINED;
}

xt_value copy_texture_to_buffer(Object &object, int argc, xt_value *argv) {
  CommandEncoder &encoder = static_cast<CommandEncoder &>(object);
  xt_value source = arg_object(argc, argv, 0);
  Texture *texture = texture_at(prop_object(source, "texture"));
  xt_value destination_value = arg_object(argc, argv, 1);
  Buffer *destination = buffer_at(prop_object(destination_value, "buffer"));
  xt_value size = arg_object(argc, argv, 2);
  if (texture == nullptr || destination == nullptr) return XT_UNDEFINED;
  flush_encoder(encoder);
  const uint32_t width = (uint32_t)prop_u64(size, "width", 0);
  const uint32_t height = (uint32_t)prop_u64(size, "height", 1);
  const uint32_t depth = (uint32_t)prop_u64(size, "depthOrArrayLayers", 1);
  const uint32_t bytes_per_row =
      (uint32_t)prop_u64(destination_value, "bytesPerRow", width * 4);
  std::vector<uint8_t> data;
  xt_value origin = prop_object(source, "origin");
  if (!read_texture_rows(*texture, (uint32_t)prop_u64(source, "mipLevel", 0),
                         (uint32_t)prop_u64(origin, "x", 0), (uint32_t)prop_u64(origin, "y", 0),
                         (uint32_t)prop_u64(origin, "z", 0), width, height, depth, bytes_per_row,
                         &data)) {
    return XT_UNDEFINED;
  }
  write_buffer_bytes(*destination, prop_u64(destination_value, "offset", 0), data);
  return XT_UNDEFINED;
}

xt_value copy_texture_to_texture(Object &object, int argc, xt_value *argv) {
  CommandEncoder &encoder = static_cast<CommandEncoder &>(object);
  xt_value source = arg_object(argc, argv, 0);
  Texture *from = texture_at(prop_object(source, "texture"));
  xt_value destination = arg_object(argc, argv, 1);
  Texture *to = texture_at(prop_object(destination, "texture"));
  xt_value size = arg_object(argc, argv, 2);
  if (from == nullptr || to == nullptr) return XT_UNDEFINED;
  xt_value origin = prop_object(source, "origin");
  SDL_GPUCommandBuffer *cmd = encoder_cmd(encoder);
  SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureLocation from_location = {};
  from_location.texture = from->gpu;
  from_location.mip_level = (Uint32)prop_u64(source, "mipLevel", 0);
  from_location.layer = (Uint32)prop_u64(origin, "z", 0);
  from_location.x = (Uint32)prop_u64(origin, "x", 0);
  from_location.y = (Uint32)prop_u64(origin, "y", 0);
  xt_value target_origin = prop_object(destination, "origin");
  SDL_GPUTextureLocation to_location = {};
  to_location.texture = to->gpu;
  to_location.mip_level = (Uint32)prop_u64(destination, "mipLevel", 0);
  to_location.layer = (Uint32)prop_u64(target_origin, "z", 0);
  to_location.x = (Uint32)prop_u64(target_origin, "x", 0);
  to_location.y = (Uint32)prop_u64(target_origin, "y", 0);
  SDL_CopyGPUTextureToTexture(pass, &from_location, &to_location,
                              (Uint32)prop_u64(size, "width", 1),
                              (Uint32)prop_u64(size, "height", 1),
                              (Uint32)prop_u64(size, "depthOrArrayLayers", 1), true);
  SDL_EndGPUCopyPass(pass);
  return XT_UNDEFINED;
}

xt_value clear_buffer(Object &object, int argc, xt_value *argv) {
  CommandEncoder &encoder = static_cast<CommandEncoder &>(object);
  Buffer *buffer = buffer_at(xt_arg(argc, argv, 0));
  const uint64_t offset = arg_u64(argc, argv, 1);
  uint64_t size = arg_u64(argc, argv, 2);
  if (buffer == nullptr) return XT_UNDEFINED;
  if (size == 0) size = buffer->size > offset ? buffer->size - offset : 0;
  flush_encoder(encoder);
  write_buffer_bytes(*buffer, offset, std::vector<uint8_t>((size_t)size, 0));
  return XT_UNDEFINED;
}

xt_value resolve_query_set(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  std::shared_ptr<Device> device = device_for(object, nullptr);
  if (device != nullptr) {
    record_error(*device, ErrorKind::Validation,
                 "resolveQuerySet: this SDL_GPU build has no query support");
  }
  return XT_UNDEFINED;
}

xt_value write_timestamp(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  std::shared_ptr<Device> device = device_for(object, nullptr);
  if (device != nullptr) {
    record_error(*device, ErrorKind::Validation,
                 "writeTimestamp: this SDL_GPU build has no timestamp support");
  }
  return XT_UNDEFINED;
}

xt_value finish_encoder(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  CommandEncoder &encoder = static_cast<CommandEncoder &>(object);
  if (encoder.finished) return XT_UNDEFINED;
  encoder.finished = true;
  auto command = std::make_shared<CommandBuffer>();
  command->kind = Kind::CommandBuffer;
  command->device_id = encoder.device_id;
  command->cmd = encoder.cmd;
  encoder.cmd = nullptr;
  return handle_of(add_object(command));
}

xt_value encoder_push_debug(Object &object, int argc, xt_value *argv) {
  CommandEncoder &encoder = static_cast<CommandEncoder &>(object);
  SDL_GPUCommandBuffer *cmd = encoder_cmd(encoder);
  const std::string name = arg_string(argc, argv, 0);
  if (cmd != nullptr) SDL_PushGPUDebugGroup(cmd, name.c_str());
  return XT_UNDEFINED;
}

xt_value encoder_pop_debug(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  CommandEncoder &encoder = static_cast<CommandEncoder &>(object);
  if (encoder.cmd != nullptr) SDL_PopGPUDebugGroup(encoder.cmd);
  return XT_UNDEFINED;
}

xt_value encoder_insert_debug(Object &object, int argc, xt_value *argv) {
  (void)object;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

}  // namespace

void register_command_methods() {
  add_getter(Kind::CommandEncoder, "label", encoder_label);
  add_method(Kind::CommandEncoder, "beginRenderPass", begin_render_pass);
  add_method(Kind::CommandEncoder, "beginComputePass", begin_compute_pass);
  add_method(Kind::CommandEncoder, "copyBufferToBuffer", copy_buffer_to_buffer);
  add_method(Kind::CommandEncoder, "copyBufferToTexture", copy_buffer_to_texture);
  add_method(Kind::CommandEncoder, "copyTextureToBuffer", copy_texture_to_buffer);
  add_method(Kind::CommandEncoder, "copyTextureToTexture", copy_texture_to_texture);
  add_method(Kind::CommandEncoder, "clearBuffer", clear_buffer);
  add_method(Kind::CommandEncoder, "resolveQuerySet", resolve_query_set);
  add_method(Kind::CommandEncoder, "writeTimestamp", write_timestamp);
  add_method(Kind::CommandEncoder, "finish", finish_encoder);
  add_method(Kind::CommandEncoder, "pushDebugGroup", encoder_push_debug);
  add_method(Kind::CommandEncoder, "popDebugGroup", encoder_pop_debug);
  add_method(Kind::CommandEncoder, "insertDebugMarker", encoder_insert_debug);
}

}  // namespace webgpu
}  // namespace xtgui
