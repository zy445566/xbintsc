/*
 * xbintsc GUI engine — WebGPU shader compilation and bind-group layouts.
 *
 * WGSL is translated to GLSL first (see `wgsl_*.cpp`); this unit turns that
 * GLSL into the bytecode the SDL_GPU backend consumes (SPIR-V on Vulkan, MSL
 * on Metal) and derives the SDL shader's resource counts from the translated
 * binding list.
 */

#include "webgpu.h"

#include <algorithm>
#include <cstring>

#include "gpu_shader_api.h"

namespace xtgui {
namespace webgpu {
namespace {

std::string stage_key(int stage, const std::string &entry) {
  return std::to_string(stage) + ":" + entry;
}

const char *stage_glsl(int stage) {
  if (stage == 1) return "fragment";
  if (stage == 2) return "compute";
  return "vertex";
}

bool build_stage_code(Device &device, ShaderModule &module, const std::string &entry, int stage,
                      StageResources *resources, bool create_shader, std::string *error) {
  if (!module.valid) {
    *error = module.error;
    return false;
  }
  std::string key = stage_key(stage, entry);
  wgsl::StageCode code;
  auto cached = module.stages.find(key);
  if (cached != module.stages.end()) {
    code = cached->second;
  } else {
    wgsl::CompileInfo info;
    if (!wgsl::translate(module.source, entry, (wgsl::Stage)stage, &code, &info)) {
      *error = info.message;
      return false;
    }
    module.stages[key] = code;
  }

  const GpuShaderApi *api = gpu_shader_api();
  if (api == nullptr) {
    *error = "shader translation is not linked (the 'gpu' link feature is inactive)";
    return false;
  }
  std::vector<uint32_t> spirv;
  std::string log;
  if (!api->compile_glsl_stage(code.glsl, stage, &spirv, &log)) {
    *error = std::string("GLSL (") + stage_glsl(stage) + ") compilation failed: " + log;
    return false;
  }
  resources->code = code;
  resources->spirv = spirv;
  resources->entry = "main";
  resources->msl.clear();

  const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(gpu_device());
  if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0) {
    std::string source;
    std::string msl_entry;
    if (!api->cross_compile_msl_stage(spirv, stage, &source, &msl_entry, &log)) {
      *error = "MSL cross-compilation failed: " + log;
      return false;
    }
    resources->msl = std::move(source);
    resources->entry = msl_entry;
    resources->format = SDL_GPU_SHADERFORMAT_MSL;
  } else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0) {
    resources->format = SDL_GPU_SHADERFORMAT_SPIRV;
  } else {
    *error = "this GPU backend consumes neither Metal nor Vulkan shaders (Direct3D 12 is not supported)";
    return false;
  }

  if (!create_shader) return true;
  SDL_GPUShaderCreateInfo info = {};
  /* The device consumes MSL text on Metal and SPIR-V words on Vulkan; point
   * `code` at whichever the backend asked for, exactly like the WebGL path.
   * `entry` already holds the cross-compiled name on Metal. */
  if (resources->format == SDL_GPU_SHADERFORMAT_MSL) {
    info.code = reinterpret_cast<const Uint8 *>(resources->msl.data());
    info.code_size = resources->msl.size();
  } else {
    info.code = reinterpret_cast<const Uint8 *>(resources->spirv.data());
    info.code_size = resources->spirv.size() * sizeof(uint32_t);
  }
  info.entrypoint = resources->entry.c_str();
  info.format = resources->format;
  info.stage = stage == 1 ? SDL_GPU_SHADERSTAGE_FRAGMENT : SDL_GPU_SHADERSTAGE_VERTEX;
  info.num_samplers = (Uint32)code.samplers.size();
  info.num_storage_textures = (Uint32)code.storage_textures.size();
  info.num_storage_buffers = (Uint32)code.storage_buffers.size();
  info.num_uniform_buffers = (Uint32)code.uniform_buffers.size();
  resources->shader = SDL_CreateGPUShader(gpu_device(), &info);
  if (resources->shader == nullptr) {
    *error = std::string("SDL_CreateGPUShader failed: ") + SDL_GetError();
    return false;
  }
  (void)device;
  return true;
}

int stage_visibility(int stage) { return stage == 1 ? 2 : (stage == 2 ? 4 : 1); }

}  // namespace

bool translate_stage(Device &device, ShaderModule &module, const std::string &entry, int stage,
                     wgsl::StageCode *code, std::string *error) {
  (void)device;
  if (!module.valid) {
    *error = module.error;
    return false;
  }
  const std::string key = stage_key(stage, entry);
  auto cached = module.stages.find(key);
  if (cached != module.stages.end()) {
    *code = cached->second;
    return true;
  }
  wgsl::CompileInfo info;
  if (!wgsl::translate(module.source, entry, (wgsl::Stage)stage, code, &info)) {
    *error = info.message;
    return false;
  }
  module.stages[key] = *code;
  return true;
}

bool compile_shader(Device &device, ShaderModule &module, const std::string &entry, int stage,
                    StageResources *resources, std::string *error) {
  return build_stage_code(device, module, entry, stage, resources, true, error);
}

bool compile_shader_code(Device &device, ShaderModule &module, const std::string &entry, int stage,
                         StageResources *resources, std::string *error) {
  return build_stage_code(device, module, entry, stage, resources, false, error);
}

std::vector<uint32_t> make_auto_layouts(
    Device &device, const std::vector<std::pair<int, wgsl::StageCode>> &stages) {
  struct Entry {
    LayoutEntry layout;
    bool set = false;
  };
  std::map<int, std::map<int, Entry>> groups;
  int max_group = 0;
  auto add = [&](int stage, int group, int binding, const char *type, const char *buffer_type) {
    if (group < 0) group = 0;
    if (group > max_group) max_group = group;
    Entry &entry = groups[group][binding];
    entry.set = true;
    entry.layout.binding = binding;
    entry.layout.visibility |= stage_visibility(stage);
    entry.layout.type = type;
    if (buffer_type != nullptr) entry.layout.buffer_type = buffer_type;
  };
  for (const auto &stage : stages) {
    const int index = stage.first;
    const wgsl::StageCode &code = stage.second;
    for (const wgsl::TextureSlot &slot : code.samplers) {
      add(index, slot.texture.group, slot.texture.binding, "texture", nullptr);
      if (slot.has_sampler) add(index, slot.sampler.group, slot.sampler.binding, "sampler", nullptr);
    }
    for (const wgsl::Binding &binding : code.storage_textures) {
      add(index, binding.group, binding.binding, "storage-texture", nullptr);
    }
    for (const wgsl::Binding &binding : code.write_storage_textures) {
      add(index, binding.group, binding.binding, "storage-texture", nullptr);
    }
    for (const wgsl::Binding &binding : code.storage_buffers) {
      add(index, binding.group, binding.binding, "buffer", "read-only-storage");
    }
    for (const wgsl::Binding &binding : code.write_storage_buffers) {
      add(index, binding.group, binding.binding, "buffer", "storage");
    }
    for (const wgsl::Binding &binding : code.uniform_buffers) {
      add(index, binding.group, binding.binding, "buffer", "uniform");
    }
  }
  std::vector<uint32_t> ids;
  for (int group = 0; group <= max_group; group++) {
    auto layout = std::make_shared<BindGroupLayout>();
    layout->kind = Kind::BindGroupLayout;
    layout->device_id = device.id;
    auto found = groups.find(group);
    if (found != groups.end()) {
      for (const auto &entry : found->second) {
        if (entry.second.set) layout->entries.push_back(entry.second.layout);
      }
    }
    ids.push_back(add_object(layout));
  }
  return ids;
}
bool build_bind_group_layout(Device &device, xt_value descriptor,
                             const std::shared_ptr<BindGroupLayout> &layout, std::string *error) {
  (void)device;
  xt_value entries = prop_object(descriptor, "entries");
  if (!XT_IS_ARRAY(entries)) {
    *error = "layout.entries must be an array";
    return false;
  }
  const int count = (int)xt_to_number(xt_array_length(entries));
  for (int i = 0; i < count; i++) {
    xt_value item = xt_get(entries, xt_number((double)i));
    if (!XT_IS_OBJECT(item)) continue;
    LayoutEntry entry;
    entry.binding = (int)prop_u64(item, "binding", 0);
    entry.visibility = (int)prop_number(item, "visibility", 7);
    xt_value buffer = xt_object_get_cstr(item, "buffer");
    xt_value texture = xt_object_get_cstr(item, "texture");
    xt_value storage = xt_object_get_cstr(item, "storageTexture");
    xt_value sampler = xt_object_get_cstr(item, "sampler");
    if (XT_IS_OBJECT(buffer)) {
      entry.type = "buffer";
      entry.buffer_type = prop_string(buffer, "type");
      if (entry.buffer_type.empty()) entry.buffer_type = "uniform";
      entry.has_dynamic_offset = prop_bool(buffer, "hasDynamicOffset", false);
      entry.min_binding_size = prop_u64(buffer, "minBindingSize", 0);
    } else if (XT_IS_OBJECT(storage)) {
      entry.type = "storage-texture";
    } else if (XT_IS_OBJECT(texture)) {
      entry.type = "texture";
    } else if (XT_IS_OBJECT(sampler)) {
      entry.type = "sampler";
    } else {
      *error = "layout entry " + std::to_string(i) + " has no resource type";
      return false;
    }
    layout->entries.push_back(entry);
  }
  return true;
}

}  // namespace webgpu
}  // namespace xtgui
