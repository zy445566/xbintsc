/*
 * xbintsc GUI engine — WebGPU adapter, device, buffer, texture and sampler
 * creation, plus the object-level methods on buffers, textures and samplers.
 */

#include <cstring>

#include "webgpu_bindings.h"
#include "wgsl.h"

namespace xtgui {
namespace webgpu {
namespace {

Device &device(Object &object) { return static_cast<Device &>(object); }

uint32_t sdl_buffer_usage(uint32_t usage) {
  uint32_t flags = 0;
  if ((usage & 32) != 0) flags |= SDL_GPU_BUFFERUSAGE_VERTEX;
  if ((usage & 16) != 0) flags |= SDL_GPU_BUFFERUSAGE_INDEX;
  if ((usage & 256) != 0) flags |= SDL_GPU_BUFFERUSAGE_INDIRECT;
  if ((usage & 128) != 0) {
    flags |= SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
             SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
  }
  return flags;
}

uint32_t sdl_texture_usage(uint32_t usage, SDL_GPUTextureFormat format) {
  uint32_t flags = 0;
  if ((usage & 4) != 0) flags |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
  if ((usage & 16) != 0) {
    flags |= Convert::is_depth_format(format) ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                                               : SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  }
  if ((usage & 8) != 0) {
    flags |= SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ |
             SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
  }
  return flags;
}

SDL_GPUTextureType texture_type(const std::string &dimension, uint32_t depth) {
  if (dimension == "3d") return SDL_GPU_TEXTURETYPE_3D;
  if (dimension == "cube") return SDL_GPU_TEXTURETYPE_CUBE;
  if (dimension == "cube-array") return SDL_GPU_TEXTURETYPE_CUBE_ARRAY;
  if (depth > 1) return SDL_GPU_TEXTURETYPE_2D_ARRAY;
  return SDL_GPU_TEXTURETYPE_2D;
}

xt_value rejected_type_error(const std::string &message) {
  xt_value text = xt_string_from_cstr(message.c_str());
  xt_value error = xt_error_ctor_by_name(xt_string_from_cstr("TypeError"), 1, &text);
  return xt_promise_reject(error);
}

xt_value lost_executor(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  const uint32_t id = (uint32_t)xt_to_number(xt_closure_env(env, 0));
  std::shared_ptr<Object> object = get_object(id, Kind::Device);
  if (object != nullptr && argc >= 1) {
    Device *device_ptr = static_cast<Device *>(object.get());
    device_ptr->lost_resolve = xt_arg(argc, argv, 0);
    xt_gc_add_root(&device_ptr->lost_resolve);
  }
  return XT_UNDEFINED;
}

/* -- limits/features objects --------------------------------------------- */

xt_value features_object() {
  xt_value features = xt_object_new();
  xt_value has = xt_closure_new((void *)+[](xt_value self, xt_value env, int32_t argc,
                                             xt_value *argv) -> xt_value {
    (void)self;
    (void)env;
    (void)argc;
    (void)argv;
    return XT_FALSE;
  }, 0, nullptr);
  xt_object_set(features, xt_string_from_cstr("has"), has);
  xt_object_set(features, xt_string_from_cstr("size"), xt_number(0));
  return features;
}

xt_value limits_object() {
  xt_value limits = xt_object_new();
  const struct {
    const char *name;
    double value;
  } kLimits[] = {
      {"maxTextureDimension1D", 8192},   {"maxTextureDimension2D", 8192},
      {"maxTextureDimension3D", 2048},   {"maxTextureArrayLayers", 256},
      {"maxBindGroups", 4},              {"maxBindingsPerBindGroup", 640},
      {"maxDynamicUniformBuffersPerPipelineLayout", 8},
      {"maxDynamicStorageBuffersPerPipelineLayout", 4},
      {"maxSampledTexturesPerShaderStage", 16},
      {"maxSamplersPerShaderStage", 16},
      {"maxStorageBuffersPerShaderStage", 8},
      {"maxStorageTexturesPerShaderStage", 4},
      {"maxUniformBuffersPerShaderStage", 4},
      {"maxUniformBufferBindingSize", 65536},
      {"maxStorageBufferBindingSize", 134217728},
      {"minUniformBufferOffsetAlignment", 256},
      {"minStorageBufferOffsetAlignment", 256},
      {"maxVertexBuffers", 8},           {"maxBufferSize", 268435456},
      {"maxVertexAttributes", 16},       {"maxVertexBufferArrayStride", 2048},
      {"maxInterStageShaderVariables", 16},
      {"maxColorAttachments", 8},        {"maxColorAttachmentBytesPerSample", 32},
      {"maxComputeWorkgroupStorageSize", 16384},
      {"maxComputeInvocationsPerWorkgroup", 256},
      {"maxComputeWorkgroupSizeX", 256}, {"maxComputeWorkgroupSizeY", 256},
      {"maxComputeWorkgroupSizeZ", 64},  {"maxComputeWorkgroupsPerDimension", 65535}};
  for (const auto &limit : kLimits) {
    xt_object_set(limits, xt_string_from_cstr(limit.name), xt_number(limit.value));
  }
  return limits;
}

xt_value adapter_info_object() {
  xt_value info = xt_object_new();
  xt_object_set(info, xt_string_from_cstr("vendor"), xt_string_from_cstr("xbintsc"));
  xt_object_set(info, xt_string_from_cstr("architecture"), xt_string_from_cstr(""));
  xt_object_set(info, xt_string_from_cstr("device"), xt_string_from_cstr(""));
  xt_object_set(info, xt_string_from_cstr("description"), xt_string_from_cstr(""));
  xt_object_set(info, xt_string_from_cstr("subgroupMinSize"), xt_number(4));
  xt_object_set(info, xt_string_from_cstr("subgroupMaxSize"), xt_number(32));
  return info;
}

/* -- destroy -------------------------------------------------------------- */

void release_stage(StageResources &stage) {
  if (stage.shader != nullptr) {
    SDL_ReleaseGPUShader(gpu_device(), stage.shader);
    stage.shader = nullptr;
  }
}

void destroy_object(const std::shared_ptr<Object> &object) {
  if (object == nullptr) return;
  switch (object->kind) {
    case Kind::Buffer: {
      auto *buffer = static_cast<Buffer *>(object.get());
      if (buffer->gpu != nullptr) SDL_ReleaseGPUBuffer(gpu_device(), buffer->gpu);
      buffer->gpu = nullptr;
      buffer->destroyed = true;
      break;
    }
    case Kind::Texture: {
      auto *texture = static_cast<Texture *>(object.get());
      if (texture->gpu != nullptr && !texture->external) {
        SDL_ReleaseGPUTexture(gpu_device(), texture->gpu);
      }
      texture->gpu = nullptr;
      texture->destroyed = true;
      break;
    }
    case Kind::Sampler: {
      auto *sampler = static_cast<Sampler *>(object.get());
      if (sampler->gpu != nullptr) SDL_ReleaseGPUSampler(gpu_device(), sampler->gpu);
      sampler->gpu = nullptr;
      break;
    }
    case Kind::RenderPipeline: {
      auto *pipeline = static_cast<RenderPipeline *>(object.get());
      if (pipeline->gpu != nullptr) SDL_ReleaseGPUGraphicsPipeline(gpu_device(), pipeline->gpu);
      release_stage(pipeline->vertex);
      release_stage(pipeline->fragment);
      break;
    }
    case Kind::ComputePipeline: {
      auto *pipeline = static_cast<ComputePipeline *>(object.get());
      if (pipeline->gpu != nullptr) SDL_ReleaseGPUComputePipeline(gpu_device(), pipeline->gpu);
      break;
    }
    default: break;
  }
}

/* -- GPU ------------------------------------------------------------------ */

xt_value gpu_request_adapter(Object &object, int argc, xt_value *argv) {
  (void)object;
  (void)argc;
  (void)argv;
  if (gpu_device() == nullptr) xt_gui_ensure_window();
  if (gpu_device() == nullptr) return resolved(XT_NULL);
  /* D3D12 consumes DXIL, which this build cannot produce (the same rule as
   * WebGL): no adapter is better than pipelines that can never be created. */
  if ((SDL_GetGPUShaderFormats(gpu_device()) &
       (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL)) == 0) {
    return resolved(XT_NULL);
  }
  auto adapter = std::make_shared<Object>();
  adapter->kind = Kind::Adapter;
  return resolved(handle_of(add_object(adapter)));
}

xt_value gpu_preferred_format(Object &object, int argc, xt_value *argv) {
  (void)object;
  (void)argc;
  (void)argv;
  return xt_string_from_cstr("bgra8unorm");
}

/* -- adapter -------------------------------------------------------------- */

xt_value adapter_request_device(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  auto device_ptr = std::make_shared<Device>();
  device_ptr->kind = Kind::Device;
  device_ptr->device_id = 0;
  const uint32_t id = add_object(device_ptr);
  device_ptr->device_id = id;

  xt_value env[1] = {xt_number((double)id)};
  xt_value executor = xt_closure_new((void *)lost_executor, 1, env);
  device_ptr->lost_promise = xt_promise_ctor(1, &executor);
  xt_gc_add_root(&device_ptr->lost_promise);
  xt_value handle = wrap_object(device_ptr, proto_for(Kind::Device));
  (void)object;
  return resolved(handle);
}

xt_value adapter_features(Object &object) {
  (void)object;
  return features_object();
}

xt_value adapter_limits(Object &object) {
  (void)object;
  return limits_object();
}

xt_value adapter_info(Object &object) {
  (void)object;
  return adapter_info_object();
}

xt_value adapter_request_info(Object &object, int argc, xt_value *argv) {
  (void)object;
  (void)argc;
  (void)argv;
  return resolved(adapter_info_object());
}

/* -- device --------------------------------------------------------------- */

xt_value device_getter(Object &object) { return queue_object(object.id); }

xt_value device_features_getter(Object &object) {
  (void)object;
  return features_object();
}

xt_value device_limits_getter(Object &object) {
  (void)object;
  return limits_object();
}

xt_value device_lost_getter(Object &object) {
  Device &dev = device(object);
  if (dev.lost) return resolved(make_device_lost(dev.lost_reason));
  if (XT_IS_OBJECT(dev.lost_promise)) return dev.lost_promise;
  return resolved(make_device_lost("unknown"));
}

xt_value device_push_error_scope(Object &object, int argc, xt_value *argv) {
  Device &dev = device(object);
  const std::string filter = arg_string(argc, argv, 0);
  ErrorScope scope;
  scope.filter = filter == "out-of-memory" ? 1 : (filter == "internal" ? 2 : 0);
  dev.scopes.push_back(scope);
  return XT_UNDEFINED;
}

xt_value device_pop_error_scope(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  Device &dev = device(object);
  if (dev.scopes.empty()) {
    return rejected_type_error("popErrorScope called without a matching pushErrorScope");
  }
  ErrorScope scope = dev.scopes.back();
  dev.scopes.pop_back();
  if (!scope.filled) return resolved(XT_NULL);
  return resolved(make_error((ErrorKind)scope.kind, scope.message));
}

xt_value device_destroy(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  Device &dev = device(object);
  if (dev.lost) return XT_UNDEFINED;
  std::vector<uint32_t> children;
  device_children(dev.id, &children);
  for (uint32_t id : children) {
    if (id == dev.id) continue;
    destroy_object(get_object(id));
    remove_object(id);
  }
  dev.lost = true;
  dev.destroyed = true;
  dev.lost_reason = "device destroyed";
  if (XT_IS_OBJECT(dev.lost_resolve)) {
    xt_value info = make_device_lost(dev.lost_reason);
    xt_call_with_this(dev.lost_resolve, XT_UNDEFINED, 1, &info);
    dev.lost_resolve = XT_UNDEFINED;
  }
  return XT_UNDEFINED;
}

xt_value device_add_listener(Object &object, int argc, xt_value *argv) {
  Device &dev = device(object);
  const std::string type = arg_string(argc, argv, 0);
  xt_value listener = xt_arg(argc, argv, 1);
  if (type != "uncapturederror" || !XT_IS_FUNCTION(listener)) return XT_UNDEFINED;
  xt_value list = listener_list(dev);
  xt_array_push(list, listener);
  return XT_UNDEFINED;
}

xt_value device_remove_listener(Object &object, int argc, xt_value *argv) {
  Device &dev = device(object);
  xt_value listener = xt_arg(argc, argv, 1);
  xt_value list = listener_list(dev);
  const uint32_t count = (uint32_t)xt_to_number(xt_array_length(list));
  for (uint32_t i = 0; i < count; i++) {
    (void)i;
    xt_value index = xt_call_method(list, xt_string_from_cstr("indexOf"), 1, &listener);
    if (XT_IS_NUMBER(index) && xt_to_number(index) >= 0) {
      xt_value args[2] = {index, xt_number(1)};
      xt_call_method(list, xt_string_from_cstr("splice"), 2, args);
      break;
    }
  }
  return XT_UNDEFINED;
}

}  // namespace

/* -- registration --------------------------------------------------------- */

void register_gpu_methods() {
  add_method(Kind::Gpu, "requestAdapter", gpu_request_adapter);
  add_method(Kind::Gpu, "getPreferredCanvasFormat", gpu_preferred_format);
  add_method(Kind::Adapter, "requestDevice", adapter_request_device);
  add_method(Kind::Adapter, "requestAdapterInfo", adapter_request_info);
  add_getter(Kind::Adapter, "features", adapter_features);
  add_getter(Kind::Adapter, "limits", adapter_limits);
  add_getter(Kind::Adapter, "info", adapter_info);
  add_getter(Kind::Adapter, "isFallbackAdapter", [](Object &object) -> xt_value {
    (void)object;
    return XT_FALSE;
  });
  add_getter(Kind::Device, "queue", device_getter);
  add_getter(Kind::Device, "features", device_features_getter);
  add_getter(Kind::Device, "limits", device_limits_getter);
  add_getter(Kind::Device, "lost", device_lost_getter);
  add_method(Kind::Device, "pushErrorScope", device_push_error_scope);
  add_method(Kind::Device, "popErrorScope", device_pop_error_scope);
  add_method(Kind::Device, "destroy", device_destroy);
  add_method(Kind::Device, "addEventListener", device_add_listener);
  add_method(Kind::Device, "removeEventListener", device_remove_listener);
}

}  // namespace webgpu
}  // namespace xtgui