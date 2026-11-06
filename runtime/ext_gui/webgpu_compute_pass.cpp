/*
 * xbintsc GUI engine — WebGPU compute passes.
 */

#include "webgpu_bindings.h"
#include "webgpu_pass.h"

namespace xtgui {
namespace webgpu {
namespace {

uint32_t object_id(xt_value value) {
  if (!XT_IS_OBJECT(value)) return 0;
  return (uint32_t)xt_to_number(xt_object_get_cstr(value, "__xt_webgpu_id"));
}

SDL_GPUCommandBuffer *command_of(ComputePassEncoder &pass) {
  std::shared_ptr<Object> object = get_object(pass.encoder, Kind::CommandEncoder);
  if (object == nullptr) return nullptr;
  CommandEncoder *encoder = static_cast<CommandEncoder *>(object.get());
  if (encoder->cmd == nullptr && !encoder->finished) {
    encoder->cmd = SDL_AcquireGPUCommandBuffer(gpu_device());
  }
  return encoder->cmd;
}

ComputePipeline *pipeline_of(ComputePassEncoder &pass) {
  std::shared_ptr<Object> object = get_object(pass.pipeline, Kind::ComputePipeline);
  return object == nullptr ? nullptr : static_cast<ComputePipeline *>(object.get());
}

xt_value compute_label(Object &object) { return xt_string_from_cstr(object.label.c_str()); }

xt_value compute_set_pipeline(Object &object, int argc, xt_value *argv) {
  ComputePassEncoder &pass = static_cast<ComputePassEncoder &>(object);
  pass.pipeline = object_id(xt_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

xt_value compute_set_bind_group(Object &object, int argc, xt_value *argv) {
  ComputePassEncoder &pass = static_cast<ComputePassEncoder &>(object);
  const int index = (int)arg_number(argc, argv, 0);
  BindGroupSlot slot;
  slot.group = object_id(xt_arg(argc, argv, 1));
  xt_value offsets = xt_arg(argc, argv, 2);
  if (XT_IS_ARRAY(offsets)) {
    const uint32_t count = (uint32_t)xt_to_number(xt_array_length(offsets));
    for (uint32_t i = 0; i < count; i++) {
      slot.dynamic_offsets.push_back(
          (uint32_t)xt_to_number(xt_array_get(offsets, xt_number((double)i))));
    }
  }
  pass.groups[index] = std::move(slot);
  return XT_UNDEFINED;
}

xt_value compute_dispatch(Object &object, int argc, xt_value *argv) {
  ComputePassEncoder &pass = static_cast<ComputePassEncoder &>(object);
  ComputePipeline *pipeline = pipeline_of(pass);
  if (pipeline == nullptr) {
    std::shared_ptr<Device> device = device_for(object, nullptr);
    if (device != nullptr) validation(*device, "dispatchWorkgroups: no compute pipeline is bound");
    return XT_UNDEFINED;
  }
  SDL_GPUCommandBuffer *command = command_of(pass);
  if (!bind_compute_resources(pass, *pipeline, command)) return XT_UNDEFINED;
  SDL_BindGPUComputePipeline(pass.pass, pipeline->gpu);
  SDL_DispatchGPUCompute(pass.pass, (Uint32)arg_number(argc, argv, 0),
                         (Uint32)arg_number(argc, argv, 1), (Uint32)arg_number(argc, argv, 2));
  return XT_UNDEFINED;
}

xt_value compute_dispatch_indirect(Object &object, int argc, xt_value *argv) {
  ComputePassEncoder &pass = static_cast<ComputePassEncoder &>(object);
  ComputePipeline *pipeline = pipeline_of(pass);
  std::shared_ptr<Object> buffer = get_object(object_id(xt_arg(argc, argv, 0)), Kind::Buffer);
  if (pipeline == nullptr || buffer == nullptr) return XT_UNDEFINED;
  Buffer *native = static_cast<Buffer *>(buffer.get());
  if (!native->has_gpu) return XT_UNDEFINED;
  SDL_GPUCommandBuffer *command = command_of(pass);
  if (!bind_compute_resources(pass, *pipeline, command)) return XT_UNDEFINED;
  SDL_BindGPUComputePipeline(pass.pass, pipeline->gpu);
  SDL_DispatchGPUComputeIndirect(pass.pass, native->gpu, (Uint32)arg_u64(argc, argv, 1));
  return XT_UNDEFINED;
}

xt_value compute_end(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  ComputePassEncoder &pass = static_cast<ComputePassEncoder &>(object);
  if (pass.ended) return XT_UNDEFINED;
  if (pass.pass != nullptr) SDL_EndGPUComputePass(pass.pass);
  pass.pass = nullptr;
  pass.ended = true;
  return XT_UNDEFINED;
}

xt_value compute_push_debug(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  ComputePassEncoder &pass = static_cast<ComputePassEncoder &>(object);
  SDL_GPUCommandBuffer *command = command_of(pass);
  if (command != nullptr) SDL_PushGPUDebugGroup(command, arg_string(argc, argv, 0).c_str());
  return XT_UNDEFINED;
}

xt_value compute_pop_debug(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  ComputePassEncoder &pass = static_cast<ComputePassEncoder &>(object);
  SDL_GPUCommandBuffer *command = command_of(pass);
  if (command != nullptr) SDL_PopGPUDebugGroup(command);
  return XT_UNDEFINED;
}

xt_value compute_noop(Object &object, int argc, xt_value *argv) {
  (void)object;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

}  // namespace

void register_compute_pass_methods() {
  add_getter(Kind::ComputePassEncoder, "label", compute_label);
  add_method(Kind::ComputePassEncoder, "setPipeline", compute_set_pipeline);
  add_method(Kind::ComputePassEncoder, "setBindGroup", compute_set_bind_group);
  add_method(Kind::ComputePassEncoder, "dispatchWorkgroups", compute_dispatch);
  add_method(Kind::ComputePassEncoder, "dispatchWorkgroupsIndirect", compute_dispatch_indirect);
  add_method(Kind::ComputePassEncoder, "end", compute_end);
  add_method(Kind::ComputePassEncoder, "pushDebugGroup", compute_push_debug);
  add_method(Kind::ComputePassEncoder, "popDebugGroup", compute_pop_debug);
  add_method(Kind::ComputePassEncoder, "insertDebugMarker", compute_noop);
}

}  // namespace webgpu
}  // namespace xtgui
