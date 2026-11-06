/*
 * xbintsc GUI engine — WebGPU buffer, texture, sampler, shader-module, bind
 * group, command-encoder and query-set creation.
 *
 * These are `GPUDevice` factory methods; they live here rather than in
 * `webgpu_device.cpp` so each unit stays within the repository's size budget.
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
    flags |= SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
             SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
             SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
  }
  return flags;
}

uint32_t sdl_texture_usage(uint32_t usage, SDL_GPUTextureFormat format) {
  return Convert::texture_usage(usage, format);
}

SDL_GPUTextureType texture_type(const std::string &dimension, uint32_t depth) {
  if (dimension == "3d") return SDL_GPU_TEXTURETYPE_3D;
  if (dimension == "cube") return SDL_GPU_TEXTURETYPE_CUBE;
  if (dimension == "cube-array") return SDL_GPU_TEXTURETYPE_CUBE_ARRAY;
  if (depth > 1) return SDL_GPU_TEXTURETYPE_2D_ARRAY;
  return SDL_GPU_TEXTURETYPE_2D;
}
xt_value device_create_buffer(Object &object, int argc, xt_value *argv) {
  Device &dev = device(object);
  xt_value descriptor = arg_object(argc, argv, 0);
  if (!XT_IS_OBJECT(descriptor)) {
    throw_type_error("createBuffer needs a descriptor");
    return XT_UNDEFINED;
  }
  const uint64_t size = prop_u64(descriptor, "size", 0);
  const uint32_t usage = (uint32_t)prop_u64(descriptor, "usage", 0);
  if (size == 0) return validation(dev, "createBuffer: size must be greater than 0");
  if (usage == 0) return validation(dev, "createBuffer: usage must not be empty");
  auto buffer = std::make_shared<Buffer>();
  buffer->kind = Kind::Buffer;
  buffer->device_id = dev.id;
  buffer->size = size;
  buffer->usage = usage;
  const bool mapped_at_creation = prop_bool(descriptor, "mappedAtCreation", false);

  uint32_t flags = sdl_buffer_usage(usage);
  const bool needs_gpu = flags != 0 || (usage & 1) != 0 || (usage & 512) != 0;
  if (needs_gpu) {
    if (flags == 0) flags = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    SDL_GPUBufferCreateInfo info = {};
    info.usage = flags;
    info.size = (Uint32)size;
    buffer->gpu = SDL_CreateGPUBuffer(gpu_device(), &info);
    if (buffer->gpu == nullptr) {
      return validation(dev, std::string("createBuffer failed: ") + SDL_GetError());
    }
    buffer->has_gpu = true;
  }
  if ((usage & 64) != 0 || mapped_at_creation || (usage & 1) != 0) {
    buffer->has_shadow = true;
    buffer->shadow.assign((size_t)size, 0);
  }
  if (mapped_at_creation) {
    buffer->mapped = buffer->shadow;
    buffer->map_state = 2;
  }
  return handle_of(add_object(buffer));
}

xt_value device_create_texture(Object &object, int argc, xt_value *argv) {
  Device &dev = device(object);
  xt_value descriptor = arg_object(argc, argv, 0);
  if (!XT_IS_OBJECT(descriptor)) {
    throw_type_error("createTexture needs a descriptor");
    return XT_UNDEFINED;
  }
  xt_value size_value = prop_object(descriptor, "size");
  const uint32_t width = (uint32_t)prop_u64(size_value, "width", 1);
  const uint32_t height = (uint32_t)prop_u64(size_value, "height", 1);
  const uint32_t depth = (uint32_t)prop_u64(size_value, "depthOrArrayLayers", 1);
  const std::string format_name = prop_string(descriptor, "format");
  const uint32_t usage = (uint32_t)prop_u64(descriptor, "usage", 0);
  SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
  if (!Convert::texture_format(format_name, &format)) {
    return validation(dev, "createTexture: unsupported format '" + format_name + "'");
  }
  if (width == 0 || height == 0) return validation(dev, "createTexture: size must not be zero");
  auto texture = std::make_shared<Texture>();
  texture->kind = Kind::Texture;
  texture->device_id = dev.id;
  texture->format = format;
  texture->width = width;
  texture->height = height;
  texture->depth = depth;
  texture->mips = (uint32_t)prop_u64(descriptor, "mipLevelCount", 1);
  texture->samples = (uint32_t)prop_u64(descriptor, "sampleCount", 1);
  texture->usage = usage;
  texture->dimension = prop_string(descriptor, "dimension");
  if (texture->dimension.empty()) texture->dimension = "2d";

  SDL_GPUTextureCreateInfo info = {};
  info.type = texture_type(texture->dimension, depth);
  info.format = format;
  info.usage = sdl_texture_usage(usage, format);
  if (info.usage == 0) info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = width;
  info.height = height;
  info.layer_count_or_depth = depth;
  info.num_levels = texture->mips;
  info.sample_count = texture->samples >= 8 ? SDL_GPU_SAMPLECOUNT_8
                        : texture->samples >= 4 ? SDL_GPU_SAMPLECOUNT_4
                        : texture->samples >= 2 ? SDL_GPU_SAMPLECOUNT_2
                                                : SDL_GPU_SAMPLECOUNT_1;
  texture->gpu = SDL_CreateGPUTexture(gpu_device(), &info);
  if (texture->gpu == nullptr) {
    return validation(dev, std::string("createTexture failed: ") + SDL_GetError());
  }
  return handle_of(add_object(texture));
}

xt_value device_create_sampler(Object &object, int argc, xt_value *argv) {
  Device &dev = device(object);
  xt_value descriptor = arg_object(argc, argv, 0);
  if (!XT_IS_OBJECT(descriptor)) {
    throw_type_error("createSampler needs a descriptor");
    return XT_UNDEFINED;
  }
  SDL_GPUSamplerCreateInfo info = {};
  info.min_filter = Convert::filter(prop_string(descriptor, "minFilter"));
  info.mag_filter = Convert::filter(prop_string(descriptor, "magFilter"));
  info.mipmap_mode = Convert::mipmap_mode(prop_string(descriptor, "mipmapFilter"));
  info.address_mode_u = Convert::address_mode(prop_string(descriptor, "addressModeU"));
  info.address_mode_v = Convert::address_mode(prop_string(descriptor, "addressModeV"));
  info.address_mode_w = Convert::address_mode(prop_string(descriptor, "addressModeW"));
  info.mip_lod_bias = 0.0f;
  info.min_lod = (float)prop_number(descriptor, "lodMinClamp", 0.0);
  info.max_lod = (float)prop_number(descriptor, "lodMaxClamp", 1000.0);
  info.max_anisotropy = (float)prop_number(descriptor, "maxAnisotropy", 1.0);
  info.enable_anisotropy = info.max_anisotropy > 1.0f;
  info.compare_op = Convert::compare(prop_string(descriptor, "compare"));
  info.enable_compare = !prop_string(descriptor, "compare").empty();
  auto sampler = std::make_shared<Sampler>();
  sampler->kind = Kind::Sampler;
  sampler->device_id = dev.id;
  sampler->compare = info.enable_compare;
  sampler->gpu = SDL_CreateGPUSampler(gpu_device(), &info);
  if (sampler->gpu == nullptr) {
    return validation(dev, std::string("createSampler failed: ") + SDL_GetError());
  }
  return handle_of(add_object(sampler));
}

xt_value device_create_shader_module(Object &object, int argc, xt_value *argv) {
  Device &dev = device(object);
  xt_value descriptor = arg_object(argc, argv, 0);
  if (!XT_IS_OBJECT(descriptor)) {
    throw_type_error("createShaderModule needs a descriptor");
    return XT_UNDEFINED;
  }
  auto module = std::make_shared<ShaderModule>();
  module->kind = Kind::ShaderModule;
  module->device_id = dev.id;
  module->source = prop_string(descriptor, "code");
  if (module->source.empty()) return validation(dev, "createShaderModule: code must not be empty");
  wgsl::CompileInfo info;
  if (!wgsl::check(module->source, &info)) {
    module->valid = false;
    module->error = info.message;
    module->error_line = info.line;
    module->error_column = info.column;
    record_error(dev, ErrorKind::Validation, info.message);
  }
  return handle_of(add_object(module));
}

xt_value device_create_bind_group_layout(Object &object, int argc, xt_value *argv) {
  Device &dev = device(object);
  xt_value descriptor = arg_object(argc, argv, 0);
  if (!XT_IS_OBJECT(descriptor)) {
    throw_type_error("createBindGroupLayout needs a descriptor");
    return XT_UNDEFINED;
  }
  auto layout = std::make_shared<BindGroupLayout>();
  layout->kind = Kind::BindGroupLayout;
  layout->device_id = dev.id;
  std::string error;
  if (!build_bind_group_layout(dev, descriptor, layout, &error)) {
    return validation(dev, error);
  }
  return handle_of(add_object(layout));
}

xt_value device_create_pipeline_layout(Object &object, int argc, xt_value *argv) {
  Device &dev = device(object);
  xt_value descriptor = arg_object(argc, argv, 0);
  if (!XT_IS_OBJECT(descriptor)) {
    throw_type_error("createPipelineLayout needs a descriptor");
    return XT_UNDEFINED;
  }
  xt_value layouts = prop_object(descriptor, "bindGroupLayouts");
  auto pipeline_layout = std::make_shared<PipelineLayout>();
  pipeline_layout->kind = Kind::PipelineLayout;
  pipeline_layout->device_id = dev.id;
  if (XT_IS_ARRAY(layouts)) {
    const uint32_t count = (uint32_t)xt_to_number(xt_array_length(layouts));
    for (uint32_t i = 0; i < count; i++) {
      xt_value item = xt_array_get(layouts, xt_number((double)i));
      const uint32_t id = (uint32_t)xt_to_number(xt_object_get_cstr(item, "__xt_webgpu_id"));
      std::shared_ptr<Object> layout = get_object(id, Kind::BindGroupLayout);
      if (layout == nullptr) return validation(dev, "createPipelineLayout: invalid layout");
      pipeline_layout->layouts.push_back(id);
    }
  }
  return handle_of(add_object(pipeline_layout));
}

xt_value device_create_bind_group(Object &object, int argc, xt_value *argv) {
  Device &dev = device(object);
  xt_value descriptor = arg_object(argc, argv, 0);
  if (!XT_IS_OBJECT(descriptor)) {
    throw_type_error("createBindGroup needs a descriptor");
    return XT_UNDEFINED;
  }
  auto group = std::make_shared<BindGroup>();
  group->kind = Kind::BindGroup;
  group->device_id = dev.id;
  xt_value layout_value = prop_object(descriptor, "layout");
  if (XT_IS_OBJECT(layout_value)) {
    group->layout =
        (uint32_t)xt_to_number(xt_object_get_cstr(layout_value, "__xt_webgpu_id"));
  }
  std::string error;
  if (!parse_bind_group(dev, descriptor, group.get(), &error)) {
    return validation(dev, error);
  }
  return handle_of(add_object(group));
}

xt_value device_create_command_encoder(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  Device &dev = device(object);
  auto encoder = std::make_shared<CommandEncoder>();
  encoder->kind = Kind::CommandEncoder;
  encoder->device_id = dev.id;
  return handle_of(add_object(encoder));
}

xt_value device_create_query_set(Object &object, int argc, xt_value *argv) {
  Device &dev = device(object);
  xt_value descriptor = arg_object(argc, argv, 0);
  if (!XT_IS_OBJECT(descriptor)) {
    throw_type_error("createQuerySet needs a descriptor");
    return XT_UNDEFINED;
  }
  auto query = std::make_shared<QuerySet>();
  query->kind = Kind::QuerySet;
  query->device_id = dev.id;
  query->type = prop_string(descriptor, "type") == "timestamp" ? 1 : 0;
  query->count = (uint32_t)prop_u64(descriptor, "count", 1);
  return handle_of(add_object(query));
}

xt_value device_import_external_texture(Object &object, int argc, xt_value *argv) {
  Device &dev = device(object);
  (void)argc;
  (void)argv;
  return validation(dev, "importExternalTexture: external image sources are not supported");
}

xt_value device_create_render_bundle_encoder(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  Device &dev = device(object);
  auto bundle = std::make_shared<RenderBundleEncoder>();
  bundle->kind = Kind::RenderBundleEncoder;
  bundle->device_id = dev.id;
  return handle_of(add_object(bundle));
}


}  // namespace

void register_resource_methods() {
  add_method(Kind::Device, "createBuffer", device_create_buffer);
  add_method(Kind::Device, "createTexture", device_create_texture);
  add_method(Kind::Device, "createSampler", device_create_sampler);
  add_method(Kind::Device, "createShaderModule", device_create_shader_module);
  add_method(Kind::Device, "createBindGroupLayout", device_create_bind_group_layout);
  add_method(Kind::Device, "createPipelineLayout", device_create_pipeline_layout);
  add_method(Kind::Device, "createBindGroup", device_create_bind_group);
  add_method(Kind::Device, "createCommandEncoder", device_create_command_encoder);
  add_method(Kind::Device, "createQuerySet", device_create_query_set);
  add_method(Kind::Device, "createRenderBundleEncoder", device_create_render_bundle_encoder);
  add_method(Kind::Device, "importExternalTexture", device_import_external_texture);
}

}  // namespace webgpu
}  // namespace xtgui