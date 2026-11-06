/*
 * xbintsc GUI engine — WebGPU render passes and render bundles.
 */

#include <cstring>

#include "webgpu_bindings.h"
#include "webgpu_pass.h"

namespace xtgui {
namespace webgpu {
namespace {

xt_value pass_label(Object &object) { return xt_string_from_cstr(object.label.c_str()); }

RenderPipeline *pipeline_of(RenderPassEncoder &pass) {
  std::shared_ptr<Object> object = get_object(pass.pipeline, Kind::RenderPipeline);
  return object == nullptr ? nullptr : static_cast<RenderPipeline *>(object.get());
}

SDL_GPUCommandBuffer *command_of(RenderPassEncoder &pass) {
  std::shared_ptr<Object> object = get_object(pass.encoder, Kind::CommandEncoder);
  if (object == nullptr) return nullptr;
  auto *encoder = static_cast<CommandEncoder *>(object.get());
  return encoder->cmd;
}

uint32_t object_id(xt_value value) {
  if (!XT_IS_OBJECT(value)) return 0;
  return (uint32_t)xt_to_number(xt_object_get_cstr(value, "__xt_webgpu_id"));
}

xt_value pass_set_pipeline(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  const uint32_t id = object_id(xt_arg(argc, argv, 0));
  std::shared_ptr<Object> pipeline = get_object(id, Kind::RenderPipeline);
  if (pipeline == nullptr || pass.pass == nullptr) return XT_UNDEFINED;
  pass.pipeline = id;
  SDL_BindGPUGraphicsPipeline(pass.pass, static_cast<RenderPipeline *>(pipeline.get())->gpu);
  return XT_UNDEFINED;
}

xt_value pass_set_bind_group(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
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

xt_value pass_set_vertex_buffer(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  const int slot = (int)arg_number(argc, argv, 0);
  std::shared_ptr<Object> buffer = get_object(object_id(xt_arg(argc, argv, 1)), Kind::Buffer);
  if (buffer == nullptr || pass.pass == nullptr || slot < 0 || slot >= 16) return XT_UNDEFINED;
  Buffer *native = static_cast<Buffer *>(buffer.get());
  pass.vertex_buffers[slot] = native->id;
  pass.vertex_offsets[slot] = arg_u64(argc, argv, 2);
  if (native->has_gpu) {
    SDL_GPUBufferBinding binding = {};
    binding.buffer = native->gpu;
    binding.offset = (Uint32)pass.vertex_offsets[slot];
    SDL_BindGPUVertexBuffers(pass.pass, (Uint32)slot, &binding, 1);
  }
  return XT_UNDEFINED;
}

xt_value pass_set_index_buffer(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  std::shared_ptr<Object> buffer = get_object(object_id(xt_arg(argc, argv, 0)), Kind::Buffer);
  if (buffer == nullptr || pass.pass == nullptr) return XT_UNDEFINED;
  Buffer *native = static_cast<Buffer *>(buffer.get());
  pass.index_buffer = native->id;
  pass.index_format = arg_string(argc, argv, 1) == "uint16" ? 16 : 32;
  pass.index_offset = arg_u64(argc, argv, 2);
  if (native->has_gpu) {
    SDL_GPUBufferBinding binding = {};
    binding.buffer = native->gpu;
    binding.offset = (Uint32)pass.index_offset;
    SDL_BindGPUIndexBuffer(pass.pass, &binding, pass.index_format == 16
                                                  ? SDL_GPU_INDEXELEMENTSIZE_16BIT
                                                  : SDL_GPU_INDEXELEMENTSIZE_32BIT);
  }
  return XT_UNDEFINED;
}

bool prepare_draw(RenderPassEncoder &pass) {
  RenderPipeline *pipeline = pipeline_of(pass);
  if (pipeline == nullptr || pass.pass == nullptr) return false;
  SDL_GPUCommandBuffer *command = command_of(pass);
  return bind_render_resources(pass, *pipeline, command);
}

xt_value pass_draw(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  if (!prepare_draw(pass)) return XT_UNDEFINED;
  SDL_DrawGPUPrimitives(pass.pass, (Uint32)arg_number(argc, argv, 0),
                        (Uint32)arg_number(argc, argv, 1), (Uint32)arg_number(argc, argv, 2),
                        (Uint32)arg_number(argc, argv, 3));
  (void)argv;
  return XT_UNDEFINED;
}

xt_value pass_draw_indexed(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  if (!prepare_draw(pass)) return XT_UNDEFINED;
  SDL_DrawGPUIndexedPrimitives(pass.pass, (Uint32)arg_number(argc, argv, 0),
                               (Uint32)arg_number(argc, argv, 1), (Uint32)arg_number(argc, argv, 2),
                               (int32_t)arg_number(argc, argv, 3), (Uint32)arg_number(argc, argv, 4));
  return XT_UNDEFINED;
}

xt_value pass_draw_indirect(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  if (!prepare_draw(pass)) return XT_UNDEFINED;
  std::shared_ptr<Object> buffer = get_object(object_id(xt_arg(argc, argv, 0)), Kind::Buffer);
  if (buffer == nullptr) return XT_UNDEFINED;
  Buffer *native = static_cast<Buffer *>(buffer.get());
  if (native->has_gpu) {
    SDL_DrawGPUPrimitivesIndirect(pass.pass, native->gpu, (Uint32)arg_u64(argc, argv, 1), 1);
  }
  return XT_UNDEFINED;
}

xt_value pass_draw_indexed_indirect(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  if (!prepare_draw(pass)) return XT_UNDEFINED;
  std::shared_ptr<Object> buffer = get_object(object_id(xt_arg(argc, argv, 0)), Kind::Buffer);
  if (buffer == nullptr) return XT_UNDEFINED;
  Buffer *native = static_cast<Buffer *>(buffer.get());
  if (native->has_gpu) {
    SDL_DrawGPUIndexedPrimitivesIndirect(pass.pass, native->gpu, (Uint32)arg_u64(argc, argv, 1), 1);
  }
  return XT_UNDEFINED;
}

xt_value pass_viewport(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  if (pass.pass == nullptr) return XT_UNDEFINED;
  SDL_GPUViewport viewport = {};
  viewport.x = (float)arg_number(argc, argv, 0);
  viewport.y = (float)arg_number(argc, argv, 1);
  viewport.w = (float)arg_number(argc, argv, 2);
  viewport.h = (float)arg_number(argc, argv, 3);
  viewport.min_depth = (float)arg_number(argc, argv, 4);
  viewport.max_depth = (float)arg_number(argc, argv, 5);
  SDL_SetGPUViewport(pass.pass, &viewport);
  return XT_UNDEFINED;
}

xt_value pass_scissor(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  if (pass.pass == nullptr) return XT_UNDEFINED;
  SDL_Rect rect = {};
  rect.x = (int)arg_number(argc, argv, 0);
  rect.y = (int)arg_number(argc, argv, 1);
  rect.w = (int)arg_number(argc, argv, 2);
  rect.h = (int)arg_number(argc, argv, 3);
  SDL_SetGPUScissor(pass.pass, &rect);
  return XT_UNDEFINED;
}

xt_value pass_blend_constant(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  xt_value color = arg_object(argc, argv, 0);
  if (pass.pass == nullptr || !XT_IS_OBJECT(color)) return XT_UNDEFINED;
  SDL_FColor value = {};
  value.r = (float)prop_number(color, "r", 0.0);
  value.g = (float)prop_number(color, "g", 0.0);
  value.b = (float)prop_number(color, "b", 0.0);
  value.a = (float)prop_number(color, "a", 0.0);
  SDL_SetGPUBlendConstants(pass.pass, value);
  return XT_UNDEFINED;
}

xt_value pass_stencil_reference(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  if (pass.pass == nullptr) return XT_UNDEFINED;
  SDL_SetGPUStencilReference(pass.pass, (Uint8)arg_number(argc, argv, 0));
  return XT_UNDEFINED;
}

xt_value pass_begin_occlusion(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  std::shared_ptr<Device> device = device_for(object, nullptr);
  if (device != nullptr) {
    record_error(*device, ErrorKind::Validation,
                 "beginOcclusionQuery: this SDL_GPU build has no query support");
  }
  return XT_UNDEFINED;
}

xt_value pass_end_occlusion(Object &object, int argc, xt_value *argv) {
  (void)object;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

xt_value pass_end(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  if (pass.ended) return XT_UNDEFINED;
  if (pass.pass != nullptr) SDL_EndGPURenderPass(pass.pass);
  pass.pass = nullptr;
  pass.ended = true;
  return XT_UNDEFINED;
}

xt_value pass_push_debug(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  SDL_GPUCommandBuffer *command = command_of(pass);
  if (command != nullptr) SDL_PushGPUDebugGroup(command, arg_string(argc, argv, 0).c_str());
  return XT_UNDEFINED;
}

xt_value pass_pop_debug(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  SDL_GPUCommandBuffer *command = command_of(pass);
  if (command != nullptr) SDL_PopGPUDebugGroup(command);
  return XT_UNDEFINED;
}

xt_value pass_execute_bundles(Object &object, int argc, xt_value *argv) {
  RenderPassEncoder &pass = static_cast<RenderPassEncoder &>(object);
  xt_value bundles = arg_object(argc, argv, 0);
  if (!XT_IS_ARRAY(bundles)) return XT_UNDEFINED;
  const uint32_t count = (uint32_t)xt_to_number(xt_array_length(bundles));
  for (uint32_t i = 0; i < count; i++) {
    xt_value item = xt_array_get(bundles, xt_number((double)i));
    std::shared_ptr<Object> object_bundle = get_object(object_id(item), Kind::RenderBundle);
    if (object_bundle == nullptr) continue;
    const auto &commands = static_cast<RenderBundle *>(object_bundle.get())->commands;
    for (const auto &command : commands) {
      switch (command.kind) {
        case 1: {
          std::shared_ptr<Object> pipeline =
              get_object(command.object, Kind::RenderPipeline);
          if (pipeline != nullptr) {
            pass.pipeline = command.object;
            SDL_BindGPUGraphicsPipeline(pass.pass,
                                        static_cast<RenderPipeline *>(pipeline.get())->gpu);
          }
          break;
        }
        case 2: {
          BindGroupSlot slot;
          slot.group = command.object;
          slot.dynamic_offsets = command.dynamic_offsets;
          pass.groups[command.slot] = std::move(slot);
          break;
        }
        case 5:
        case 6: {
          if (!prepare_draw(pass)) break;
          if (command.kind == 5) {
            SDL_DrawGPUPrimitives(pass.pass, command.count, command.instances, command.first, 0);
          } else {
            SDL_DrawGPUIndexedPrimitives(pass.pass, command.count, command.instances, command.first,
                                         0, 0);
          }
          break;
        }
        default: break;
      }
    }
  }
  return XT_UNDEFINED;
}

/* -- render bundle encoder ------------------------------------------------ */

xt_value bundle_set_pipeline(Object &object, int argc, xt_value *argv) {
  RenderBundleEncoder &bundle = static_cast<RenderBundleEncoder &>(object);
  RenderBundleEncoder::Command command;
  command.kind = 1;
  command.object = object_id(xt_arg(argc, argv, 0));
  bundle.commands.push_back(command);
  return XT_UNDEFINED;
}

xt_value bundle_set_bind_group(Object &object, int argc, xt_value *argv) {
  RenderBundleEncoder &bundle = static_cast<RenderBundleEncoder &>(object);
  RenderBundleEncoder::Command command;
  command.kind = 2;
  command.slot = (int)arg_number(argc, argv, 0);
  command.object = object_id(xt_arg(argc, argv, 1));
  xt_value offsets = xt_arg(argc, argv, 2);
  if (XT_IS_ARRAY(offsets)) {
    const uint32_t count = (uint32_t)xt_to_number(xt_array_length(offsets));
    for (uint32_t i = 0; i < count; i++) {
      command.dynamic_offsets.push_back(
          (uint32_t)xt_to_number(xt_array_get(offsets, xt_number((double)i))));
    }
  }
  bundle.commands.push_back(command);
  return XT_UNDEFINED;
}

xt_value bundle_draw(Object &object, int argc, xt_value *argv) {
  RenderBundleEncoder &bundle = static_cast<RenderBundleEncoder &>(object);
  RenderBundleEncoder::Command command;
  command.kind = 5;
  command.count = (uint32_t)arg_number(argc, argv, 0);
  command.instances = (uint32_t)arg_number(argc, argv, 1);
  command.first = (uint32_t)arg_number(argc, argv, 2);
  bundle.commands.push_back(command);
  return XT_UNDEFINED;
}

xt_value bundle_draw_indexed(Object &object, int argc, xt_value *argv) {
  RenderBundleEncoder &bundle = static_cast<RenderBundleEncoder &>(object);
  RenderBundleEncoder::Command command;
  command.kind = 6;
  command.count = (uint32_t)arg_number(argc, argv, 0);
  command.instances = (uint32_t)arg_number(argc, argv, 1);
  command.first = (uint32_t)arg_number(argc, argv, 2);
  bundle.commands.push_back(command);
  return XT_UNDEFINED;
}

xt_value bundle_finish(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  RenderBundleEncoder &bundle = static_cast<RenderBundleEncoder &>(object);
  if (bundle.finished) return XT_UNDEFINED;
  bundle.finished = true;
  auto result = std::make_shared<RenderBundle>();
  result->kind = Kind::RenderBundle;
  result->device_id = bundle.device_id;
  result->commands = bundle.commands;
  return handle_of(add_object(result));
}

xt_value bundle_noop(Object &object, int argc, xt_value *argv) {
  (void)object;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

}  // namespace

void register_render_pass_methods() {
  add_getter(Kind::RenderPassEncoder, "label", pass_label);
  add_method(Kind::RenderPassEncoder, "setPipeline", pass_set_pipeline);
  add_method(Kind::RenderPassEncoder, "setBindGroup", pass_set_bind_group);
  add_method(Kind::RenderPassEncoder, "setVertexBuffer", pass_set_vertex_buffer);
  add_method(Kind::RenderPassEncoder, "setIndexBuffer", pass_set_index_buffer);
  add_method(Kind::RenderPassEncoder, "draw", pass_draw);
  add_method(Kind::RenderPassEncoder, "drawIndexed", pass_draw_indexed);
  add_method(Kind::RenderPassEncoder, "drawIndirect", pass_draw_indirect);
  add_method(Kind::RenderPassEncoder, "drawIndexedIndirect", pass_draw_indexed_indirect);
  add_method(Kind::RenderPassEncoder, "setViewport", pass_viewport);
  add_method(Kind::RenderPassEncoder, "setScissorRect", pass_scissor);
  add_method(Kind::RenderPassEncoder, "setBlendConstant", pass_blend_constant);
  add_method(Kind::RenderPassEncoder, "setStencilReference", pass_stencil_reference);
  add_method(Kind::RenderPassEncoder, "beginOcclusionQuery", pass_begin_occlusion);
  add_method(Kind::RenderPassEncoder, "endOcclusionQuery", pass_end_occlusion);
  add_method(Kind::RenderPassEncoder, "executeBundles", pass_execute_bundles);
  add_method(Kind::RenderPassEncoder, "end", pass_end);
  add_method(Kind::RenderPassEncoder, "pushDebugGroup", pass_push_debug);
  add_method(Kind::RenderPassEncoder, "popDebugGroup", pass_pop_debug);
  add_method(Kind::RenderPassEncoder, "insertDebugMarker", bundle_noop);

  add_method(Kind::RenderBundleEncoder, "setPipeline", bundle_set_pipeline);
  add_method(Kind::RenderBundleEncoder, "setBindGroup", bundle_set_bind_group);
  add_method(Kind::RenderBundleEncoder, "setVertexBuffer", bundle_noop);
  add_method(Kind::RenderBundleEncoder, "setIndexBuffer", bundle_noop);
  add_method(Kind::RenderBundleEncoder, "draw", bundle_draw);
  add_method(Kind::RenderBundleEncoder, "drawIndexed", bundle_draw_indexed);
  add_method(Kind::RenderBundleEncoder, "drawIndirect", bundle_noop);
  add_method(Kind::RenderBundleEncoder, "drawIndexedIndirect", bundle_noop);
  add_method(Kind::RenderBundleEncoder, "insertDebugMarker", bundle_noop);
  add_method(Kind::RenderBundleEncoder, "pushDebugGroup", bundle_noop);
  add_method(Kind::RenderBundleEncoder, "popDebugGroup", bundle_noop);
  add_method(Kind::RenderBundleEncoder, "finish", bundle_finish);
}

}  // namespace webgpu
}  // namespace xtgui
