/*
 * xbintsc GUI engine — WebGPU shader modules, bind groups and pipelines.
 */

#include <cstring>
#include <vector>

#include "webgpu_bindings.h"
#include "wgsl.h"

namespace xtgui {
namespace webgpu {
namespace {

uint32_t handle_id(xt_value value) {
  if (!XT_IS_OBJECT(value)) return 0;
  return (uint32_t)xt_to_number(xt_object_get_cstr(value, "__xt_webgpu_id"));
}

xt_value label_of(Object &object) { return xt_string_from_cstr(object.label.c_str()); }

SDL_GPUStencilOp stencil_op(const std::string &name) {
  if (name == "zero") return SDL_GPU_STENCILOP_ZERO;
  if (name == "replace") return SDL_GPU_STENCILOP_REPLACE;
  if (name == "invert") return SDL_GPU_STENCILOP_INVERT;
  if (name == "increment-clamp") return SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP;
  if (name == "decrement-clamp") return SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP;
  if (name == "increment-wrap") return SDL_GPU_STENCILOP_INCREMENT_AND_WRAP;
  if (name == "decrement-wrap") return SDL_GPU_STENCILOP_DECREMENT_AND_WRAP;
  return SDL_GPU_STENCILOP_KEEP;
}

void fill_stencil(xt_value value, SDL_GPUStencilOpState *state) {
  if (!XT_IS_OBJECT(value)) {
    state->compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    state->fail_op = SDL_GPU_STENCILOP_KEEP;
    state->depth_fail_op = SDL_GPU_STENCILOP_KEEP;
    state->pass_op = SDL_GPU_STENCILOP_KEEP;
    return;
  }
  state->compare_op = Convert::compare(prop_string(value, "compare"));
  state->fail_op = stencil_op(prop_string(value, "failOp"));
  state->depth_fail_op = stencil_op(prop_string(value, "depthFailOp"));
  state->pass_op = stencil_op(prop_string(value, "passOp"));
}

void fill_blend(xt_value target, SDL_GPUColorTargetBlendState *state, uint32_t write_mask) {
  xt_value blend = prop_object(target, "blend");
  if (!XT_IS_OBJECT(blend)) {
    state->enable_blend = false;
    state->color_write_mask = write_mask;
    state->enable_color_write_mask = write_mask != 15;
    return;
  }
  xt_value color = prop_object(blend, "color");
  xt_value alpha = prop_object(blend, "alpha");
  state->enable_blend = true;
  state->src_color_blendfactor = Convert::blend_factor(prop_string(color, "srcFactor"));
  state->dst_color_blendfactor = Convert::blend_factor(prop_string(color, "dstFactor"));
  state->color_blend_op = Convert::blend_op(prop_string(color, "operation"));
  state->src_alpha_blendfactor = Convert::blend_factor(prop_string(alpha, "srcFactor"));
  state->dst_alpha_blendfactor = Convert::blend_factor(prop_string(alpha, "dstFactor"));
  state->alpha_blend_op = Convert::blend_op(prop_string(alpha, "operation"));
  state->color_write_mask = write_mask;
  state->enable_color_write_mask = write_mask != 15;
}

bool build_color_targets(xt_value targets, std::vector<SDL_GPUColorTargetDescription> *out,
                         std::string *error) {
  if (!XT_IS_ARRAY(targets)) return true;
  const uint32_t count = (uint32_t)xt_to_number(xt_array_length(targets));
  for (uint32_t i = 0; i < count; i++) {
    xt_value target = xt_array_get(targets, xt_number((double)i));
    const std::string format = prop_string(target, "format");
    SDL_GPUColorTargetDescription description = {};
    if (!Convert::texture_format(format, &description.format)) {
      *error = "unsupported color target format '" + format + "'";
      return false;
    }
    fill_blend(target, &description.blend_state, (uint32_t)prop_u64(target, "writeMask", 15));
    out->push_back(description);
  }
  return true;
}

bool make_pipeline_layout(Device &device, xt_value descriptor,
                          const std::vector<std::pair<int, wgsl::StageCode>> &stages,
                          uint32_t *layout_id, std::string *error) {
  xt_value layout = prop_object(descriptor, "layout");
  if (XT_IS_OBJECT(layout)) {
    const uint32_t id = handle_id(layout);
    if (get_object(id, Kind::PipelineLayout) == nullptr) {
      *error = "the pipeline layout is not a GPUPipelineLayout";
      return false;
    }
    *layout_id = id;
    return true;
  }
  auto pipeline_layout = std::make_shared<PipelineLayout>();
  pipeline_layout->kind = Kind::PipelineLayout;
  pipeline_layout->device_id = device.id;
  pipeline_layout->layouts = make_auto_layouts(device, stages);
  *layout_id = add_object(pipeline_layout);
  return true;
}

bool build_vertex_input(xt_value vertex, SDL_GPUVertexInputState *state,
                        std::vector<SDL_GPUVertexBufferDescription> *buffers,
                        std::vector<SDL_GPUVertexAttribute> *attributes, std::string *error) {
  xt_value list = prop_object(vertex, "buffers");
  if (!XT_IS_ARRAY(list)) return true;
  const uint32_t count = (uint32_t)xt_to_number(xt_array_length(list));
  for (uint32_t i = 0; i < count; i++) {
    xt_value buffer = xt_array_get(list, xt_number((double)i));
    SDL_GPUVertexBufferDescription description = {};
    description.slot = i;
    description.pitch = (Uint32)prop_u64(buffer, "arrayStride", 0);
    description.input_rate = prop_string(buffer, "stepMode") == "instance"
                                 ? SDL_GPU_VERTEXINPUTRATE_INSTANCE
                                 : SDL_GPU_VERTEXINPUTRATE_VERTEX;
    buffers->push_back(description);
    xt_value attrs = prop_object(buffer, "attributes");
    if (!XT_IS_ARRAY(attrs)) continue;
    const uint32_t attr_count = (uint32_t)xt_to_number(xt_array_length(attrs));
    for (uint32_t a = 0; a < attr_count; a++) {
      xt_value attribute = xt_array_get(attrs, xt_number((double)a));
      SDL_GPUVertexAttribute entry = {};
      entry.location = (Uint32)prop_u64(attribute, "shaderLocation", 0);
      entry.buffer_slot = i;
      entry.offset = (Uint32)prop_u64(attribute, "offset", 0);
      const std::string format = prop_string(attribute, "format");
      if (!Convert::vertex_format(format, &entry.format)) {
        *error = "unsupported vertex format '" + format + "'";
        return false;
      }
      attributes->push_back(entry);
    }
  }
  state->vertex_buffer_descriptions = buffers->data();
  state->num_vertex_buffers = (Uint32)buffers->size();
  state->vertex_attributes = attributes->data();
  state->num_vertex_attributes = (Uint32)attributes->size();
  return true;
}

/* -- bind groups ---------------------------------------------------------- */

bool parse_bind_group_impl(Device &device, xt_value descriptor, BindGroup *out, std::string *error) {
  std::shared_ptr<Object> layout_object =
      get_object(handle_id(prop_object(descriptor, "layout")), Kind::BindGroupLayout);
  if (layout_object == nullptr) {
    *error = "createBindGroup: layout is not a GPUBindGroupLayout";
    return false;
  }
  BindGroupLayout *layout = static_cast<BindGroupLayout *>(layout_object.get());
  out->layout = layout->id;
  xt_value entries = prop_object(descriptor, "entries");
  if (!XT_IS_ARRAY(entries)) {
    *error = "createBindGroup: entries must be an array";
    return false;
  }
  const uint32_t count = (uint32_t)xt_to_number(xt_array_length(entries));
  for (uint32_t i = 0; i < count; i++) {
    xt_value item = xt_array_get(entries, xt_number((double)i));
    BindGroupEntry entry;
    entry.binding = (int)prop_u64(item, "binding", 0);
    const LayoutEntry *expected = nullptr;
    for (const LayoutEntry &candidate : layout->entries) {
      if (candidate.binding == entry.binding) expected = &candidate;
    }
    if (expected == nullptr) {
      *error = "createBindGroup: binding " + std::to_string(entry.binding) + " is not in the layout";
      return false;
    }
    xt_value resource = prop_object(item, "resource");
    xt_value buffer = prop_object(resource, "buffer");
    xt_value sampler = prop_object(resource, "sampler");
    xt_value view = prop_object(resource, "textureView");
    /* A WebGPU bind-group entry names the sampler/view object directly; accept
     * the `{ sampler }` / `{ textureView }` wrappers too. */
    if (XT_IS_OBJECT(resource) && !XT_IS_OBJECT(buffer) && !XT_IS_OBJECT(sampler) &&
        !XT_IS_OBJECT(view)) {
      const xt_value kind_value = xt_object_get_cstr(resource, "__xt_webgpu_kind");
      const int kind = XT_IS_NUMBER(kind_value) ? (int)xt_to_number(kind_value) : 0;
      if (kind == (int)Kind::Sampler) sampler = resource;
      else if (kind == (int)Kind::TextureView) view = resource;
    }
    if (XT_IS_OBJECT(buffer)) {
      std::shared_ptr<Object> object = get_object(handle_id(buffer), Kind::Buffer);
      if (object == nullptr) {
        *error = "createBindGroup: resource.buffer is not a GPUBuffer";
        return false;
      }
      entry.buffer = object->id;
      entry.offset = prop_u64(resource, "offset", 0);
      entry.size = prop_u64(resource, "size", 0);
    } else if (XT_IS_OBJECT(sampler)) {
      std::shared_ptr<Object> object = get_object(handle_id(sampler), Kind::Sampler);
      if (object == nullptr) {
        *error = "createBindGroup: resource.sampler is not a GPUSampler";
        return false;
      }
      entry.sampler = object->id;
    } else if (XT_IS_OBJECT(view)) {
      std::shared_ptr<Object> object = get_object(handle_id(view), Kind::TextureView);
      if (object == nullptr) {
        *error = "createBindGroup: resource.textureView is not a GPUTextureView";
        return false;
      }
      entry.view = object->id;
    } else {
      *error = "createBindGroup: entry " + std::to_string(entry.binding) + " has no resource";
      return false;
    }
    out->entries.push_back(entry);
  }
  (void)device;
  return true;
}

/* -- shader modules ------------------------------------------------------- */

xt_value shader_compilation_info(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  ShaderModule &module = static_cast<ShaderModule &>(object);
  xt_value info = xt_object_new();
  xt_value messages = xt_array_new(0, nullptr);
  if (!module.valid) {
    xt_value message = xt_object_new();
    xt_object_set(message, xt_string_from_cstr("message"), xt_string_from_cstr(module.error.c_str()));
    xt_object_set(message, xt_string_from_cstr("type"), xt_string_from_cstr("error"));
    xt_object_set(message, xt_string_from_cstr("lineNum"), xt_number(module.error_line));
    xt_object_set(message, xt_string_from_cstr("linePos"), xt_number(module.error_column));
    xt_object_set(message, xt_string_from_cstr("offset"), xt_number(0));
    xt_object_set(message, xt_string_from_cstr("length"), xt_number(0));
    xt_array_push(messages, message);
  }
  xt_object_set(info, xt_string_from_cstr("messages"), messages);
  return resolved(info);
}

/* -- pipelines ------------------------------------------------------------ */

xt_value create_render_pipeline(Object &object, int argc, xt_value *argv) {
  Device &device = static_cast<Device &>(object);
  xt_value descriptor = arg_object(argc, argv, 0);
  if (!XT_IS_OBJECT(descriptor)) {
    throw_type_error("createRenderPipeline needs a descriptor");
    return XT_UNDEFINED;
  }
  xt_value vertex = prop_object(descriptor, "vertex");
  std::shared_ptr<Object> vertex_module =
      get_object(handle_id(prop_object(vertex, "module")), Kind::ShaderModule);
  if (vertex_module == nullptr) {
    return validation(device, "createRenderPipeline: vertex.module is not a GPUShaderModule");
  }
  auto pipeline = std::make_shared<RenderPipeline>();
  pipeline->kind = Kind::RenderPipeline;
  pipeline->device_id = device.id;
  std::string error;
  if (!compile_shader(device, *static_cast<ShaderModule *>(vertex_module.get()),
                      prop_string(vertex, "entryPoint"), 0, &pipeline->vertex, &error)) {
    return validation(device, error);
  }

  std::vector<std::pair<int, wgsl::StageCode>> stages;
  stages.emplace_back(0, pipeline->vertex.code);
  std::vector<SDL_GPUColorTargetDescription> targets;
  xt_value fragment = prop_object(descriptor, "fragment");
  if (XT_IS_OBJECT(fragment)) {
    std::shared_ptr<Object> fragment_module =
        get_object(handle_id(prop_object(fragment, "module")), Kind::ShaderModule);
    if (fragment_module == nullptr) {
      return validation(device, "createRenderPipeline: fragment.module is not a GPUShaderModule");
    }
    if (!compile_shader(device, *static_cast<ShaderModule *>(fragment_module.get()),
                        prop_string(fragment, "entryPoint"), 1, &pipeline->fragment, &error)) {
      return validation(device, error);
    }
    pipeline->has_fragment = true;
    stages.emplace_back(1, pipeline->fragment.code);
    if (!build_color_targets(prop_object(fragment, "targets"), &targets, &error)) {
      return validation(device, error);
    }
  }
  if (!make_pipeline_layout(device, descriptor, stages, &pipeline->layout, &error)) {
    return validation(device, error);
  }

  std::vector<SDL_GPUVertexBufferDescription> buffers;
  std::vector<SDL_GPUVertexAttribute> attributes;
  SDL_GPUVertexInputState input = {};
  if (!build_vertex_input(vertex, &input, &buffers, &attributes, &error)) {
    return validation(device, error);
  }

  xt_value primitive = prop_object(descriptor, "primitive");
  xt_value depth = prop_object(descriptor, "depthStencil");
  xt_value multisample = prop_object(descriptor, "multisample");
  const std::string depth_format_name = prop_string(depth, "format");

  SDL_GPUGraphicsPipelineCreateInfo info = {};
  info.vertex_shader = pipeline->vertex.shader;
  info.fragment_shader = pipeline->has_fragment ? pipeline->fragment.shader : nullptr;
  info.vertex_input_state = input;
  info.primitive_type = Convert::topology(prop_string(primitive, "topology"));
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  const std::string cull = prop_string(primitive, "cullMode");
  info.rasterizer_state.cull_mode = cull == "front" ? SDL_GPU_CULLMODE_FRONT
                                     : cull == "back" ? SDL_GPU_CULLMODE_BACK
                                                      : SDL_GPU_CULLMODE_NONE;
  info.rasterizer_state.front_face = prop_string(primitive, "frontFace") == "cw"
                                         ? SDL_GPU_FRONTFACE_CLOCKWISE
                                         : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  info.rasterizer_state.enable_depth_bias = prop_bool(depth, "depthBias", 0) != 0;
  info.rasterizer_state.depth_bias_constant_factor =
      (float)prop_number(depth, "depthBiasConstantFactor", 0.0);
  info.rasterizer_state.depth_bias_clamp = (float)prop_number(depth, "depthBiasClamp", 0.0);
  info.rasterizer_state.depth_bias_slope_factor =
      (float)prop_number(depth, "depthBiasSlopeScale", 0.0);
  info.rasterizer_state.enable_depth_clip = true;
  const int samples = (int)prop_u64(multisample, "count", 1);
  info.multisample_state.sample_count = samples >= 8   ? SDL_GPU_SAMPLECOUNT_8
                                        : samples >= 4 ? SDL_GPU_SAMPLECOUNT_4
                                        : samples >= 2 ? SDL_GPU_SAMPLECOUNT_2
                                                       : SDL_GPU_SAMPLECOUNT_1;
  info.multisample_state.sample_mask = 0;
  info.multisample_state.enable_mask = false;
  info.depth_stencil_state.enable_depth_test = false;
  if (XT_IS_OBJECT(depth) && !depth_format_name.empty()) {
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
    if (!Convert::texture_format(depth_format_name, &format)) {
      return validation(device, "unsupported depth format '" + depth_format_name + "'");
    }
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = format;
    const std::string compare = prop_string(depth, "depthCompare");
    info.depth_stencil_state.compare_op = Convert::compare(compare);
    info.depth_stencil_state.enable_depth_test = compare != "always" && !compare.empty();
    info.depth_stencil_state.enable_depth_write = prop_bool(depth, "depthWriteEnabled", false);
    info.depth_stencil_state.enable_stencil_test =
        XT_IS_OBJECT(prop_object(depth, "stencilFront")) ||
        XT_IS_OBJECT(prop_object(depth, "stencilBack"));
    fill_stencil(prop_object(depth, "stencilFront"),
                 &info.depth_stencil_state.front_stencil_state);
    fill_stencil(prop_object(depth, "stencilBack"), &info.depth_stencil_state.back_stencil_state);
    info.depth_stencil_state.compare_mask = 0xFF;
    info.depth_stencil_state.write_mask = 0xFF;
  }
  info.target_info.color_target_descriptions = targets.empty() ? nullptr : targets.data();
  info.target_info.num_color_targets = (Uint32)targets.size();

  pipeline->gpu = SDL_CreateGPUGraphicsPipeline(gpu_device(), &info);
  if (pipeline->gpu == nullptr) {
    return validation(device, std::string("createRenderPipeline failed: ") + SDL_GetError());
  }
  return handle_of(add_object(pipeline));
}

xt_value create_compute_pipeline(Object &object, int argc, xt_value *argv) {
  Device &device = static_cast<Device &>(object);
  xt_value descriptor = arg_object(argc, argv, 0);
  if (!XT_IS_OBJECT(descriptor)) {
    throw_type_error("createComputePipeline needs a descriptor");
    return XT_UNDEFINED;
  }
  xt_value compute = prop_object(descriptor, "compute");
  std::shared_ptr<Object> module =
      get_object(handle_id(prop_object(compute, "module")), Kind::ShaderModule);
  if (module == nullptr) {
    return validation(device, "createComputePipeline: compute.module is not a GPUShaderModule");
  }
  auto pipeline = std::make_shared<ComputePipeline>();
  pipeline->kind = Kind::ComputePipeline;
  pipeline->device_id = device.id;
  std::string error;
  if (!compile_shader_code(device, *static_cast<ShaderModule *>(module.get()),
                           prop_string(compute, "entryPoint"), 2, &pipeline->stage, &error)) {
    return validation(device, error);
  }
  std::vector<std::pair<int, wgsl::StageCode>> stages;
  stages.emplace_back(2, pipeline->stage.code);
  if (!make_pipeline_layout(device, descriptor, stages, &pipeline->layout, &error)) {
    return validation(device, error);
  }

  SDL_GPUComputePipelineCreateInfo info = {};
  if (pipeline->stage.format == SDL_GPU_SHADERFORMAT_MSL) {
    info.code = reinterpret_cast<const Uint8 *>(pipeline->stage.msl.data());
    info.code_size = pipeline->stage.msl.size();
  } else {
    info.code = reinterpret_cast<const Uint8 *>(pipeline->stage.spirv.data());
    info.code_size = pipeline->stage.spirv.size() * sizeof(uint32_t);
  }
  info.entrypoint = pipeline->stage.entry.c_str();
  info.format = pipeline->stage.format;
  info.num_samplers = (Uint32)pipeline->stage.code.samplers.size();
  info.num_readonly_storage_textures = (Uint32)pipeline->stage.code.storage_textures.size();
  info.num_readonly_storage_buffers = (Uint32)pipeline->stage.code.storage_buffers.size();
  info.num_readwrite_storage_textures =
      (Uint32)pipeline->stage.code.write_storage_textures.size();
  info.num_readwrite_storage_buffers = (Uint32)pipeline->stage.code.write_storage_buffers.size();
  info.num_uniform_buffers = (Uint32)pipeline->stage.code.uniform_buffers.size();
  info.threadcount_x = (Uint32)pipeline->stage.code.workgroup_size[0];
  info.threadcount_y = (Uint32)pipeline->stage.code.workgroup_size[1];
  info.threadcount_z = (Uint32)pipeline->stage.code.workgroup_size[2];
  pipeline->gpu = SDL_CreateGPUComputePipeline(gpu_device(), &info);
  if (pipeline->gpu == nullptr) {
    return validation(device, std::string("createComputePipeline failed: ") + SDL_GetError());
  }
  return handle_of(add_object(pipeline));
}

xt_value pipeline_bind_group_layout(Object &object, int argc, xt_value *argv) {
  const int index = (int)arg_number(argc, argv, 0);
  uint32_t layout_id = 0;
  if (object.kind == Kind::ComputePipeline) {
    layout_id = static_cast<ComputePipeline &>(object).layout;
  } else {
    layout_id = static_cast<RenderPipeline &>(object).layout;
  }
  std::shared_ptr<Object> object_layout = get_object(layout_id, Kind::PipelineLayout);
  if (object_layout == nullptr) return XT_UNDEFINED;
  PipelineLayout *layout = static_cast<PipelineLayout *>(object_layout.get());
  if (index < 0 || (size_t)index >= layout->layouts.size()) return XT_UNDEFINED;
  return handle_of(layout->layouts[(size_t)index]);
}

xt_value pipeline_async_render(Object &object, int argc, xt_value *argv) {
  xt_value created = create_render_pipeline(object, argc, argv);
  return resolved(created);
}

xt_value pipeline_async_compute(Object &object, int argc, xt_value *argv) {
  xt_value created = create_compute_pipeline(object, argc, argv);
  return resolved(created);
}

}  // namespace

bool parse_bind_group(Device &device, xt_value descriptor, BindGroup *out, std::string *error) {
  return parse_bind_group_impl(device, descriptor, out, error);
}

void register_pipeline_methods() {
  add_getter(Kind::ShaderModule, "label", label_of);
  add_method(Kind::ShaderModule, "getCompilationInfo", shader_compilation_info);
  add_getter(Kind::BindGroupLayout, "label", label_of);
  add_getter(Kind::PipelineLayout, "label", label_of);
  add_getter(Kind::BindGroup, "label", label_of);
  add_getter(Kind::RenderPipeline, "label", label_of);
  add_getter(Kind::ComputePipeline, "label", label_of);
  add_method(Kind::RenderPipeline, "getBindGroupLayout", pipeline_bind_group_layout);
  add_method(Kind::ComputePipeline, "getBindGroupLayout", pipeline_bind_group_layout);
  add_method(Kind::Device, "createRenderPipeline", create_render_pipeline);
  add_method(Kind::Device, "createComputePipeline", create_compute_pipeline);
  add_method(Kind::Device, "createRenderPipelineAsync", pipeline_async_render);
  add_method(Kind::Device, "createComputePipelineAsync", pipeline_async_compute);
}

}  // namespace webgpu
}  // namespace xtgui
