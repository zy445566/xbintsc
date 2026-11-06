/*
 * xbintsc GUI engine — resolving WebGPU bind groups to SDL_GPU bindings.
 */

#include "webgpu_pass.h"

#include <vector>

#include "webgpu_bindings.h"

namespace xtgui {
namespace webgpu {
namespace {

Device *device_of_pass(uint32_t id) {
  std::shared_ptr<Object> object = get_object(id, Kind::Device);
  return object == nullptr ? nullptr : static_cast<Device *>(object.get());
}

SDL_GPUTexture *texture_of(uint32_t texture_id) {
  std::shared_ptr<Object> object = get_object(texture_id, Kind::Texture);
  if (object == nullptr) return nullptr;
  return static_cast<Texture *>(object.get())->gpu;
}

SDL_GPUBuffer *buffer_of(uint32_t buffer_id) {
  std::shared_ptr<Object> object = get_object(buffer_id, Kind::Buffer);
  if (object == nullptr) return nullptr;
  Buffer *buffer = static_cast<Buffer *>(object.get());
  return buffer->has_gpu ? buffer->gpu : nullptr;
}

SDL_GPUSampler *sampler_of(uint32_t sampler_id) {
  std::shared_ptr<Object> object = get_object(sampler_id, Kind::Sampler);
  if (object == nullptr) return nullptr;
  return static_cast<Sampler *>(object.get())->gpu;
}

void bind_samplers(SDL_GPURenderPass *pass, const wgsl::StageCode &code,
                   const std::map<int, BindGroupSlot> &groups, bool vertex, Device *device) {
  std::vector<SDL_GPUTextureSamplerBinding> bindings;
  bindings.reserve(code.samplers.size());
  for (const wgsl::TextureSlot &slot : code.samplers) {
    SDL_GPUTexture *texture = nullptr;
    SDL_GPUSampler *sampler = nullptr;
    int dynamic = 0;
    const BindGroupEntry *entry =
        bound_entry(groups, slot.texture.group, slot.texture.binding, &dynamic);
    if (entry != nullptr) texture = view_texture(entry->view);
    if (slot.has_sampler) {
      const BindGroupEntry *sampler_entry =
          bound_entry(groups, slot.sampler.group, slot.sampler.binding, &dynamic);
      if (sampler_entry != nullptr) sampler = sampler_of(sampler_entry->sampler);
    }
    if (texture == nullptr && device != nullptr) texture = fallback_texture(*device);
    if (sampler == nullptr && device != nullptr) sampler = fallback_sampler(*device);
    SDL_GPUTextureSamplerBinding binding = {};
    binding.texture = texture;
    binding.sampler = sampler;
    bindings.push_back(binding);
  }
  if (bindings.empty()) return;
  if (vertex) {
    SDL_BindGPUVertexSamplers(pass, 0, bindings.data(), (Uint32)bindings.size());
  } else {
    SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), (Uint32)bindings.size());
  }
}

void bind_storage_buffers(SDL_GPURenderPass *pass, const wgsl::StageCode &code,
                          const std::map<int, BindGroupSlot> &groups, bool vertex) {
  std::vector<SDL_GPUBuffer *> buffers;
  for (const wgsl::Binding &binding : code.storage_buffers) {
    int dynamic = 0;
    const BindGroupEntry *entry = bound_entry(groups, binding.group, binding.binding, &dynamic);
    buffers.push_back(entry != nullptr ? buffer_of(entry->buffer) : nullptr);
  }
  if (buffers.empty()) return;
  if (vertex) {
    SDL_BindGPUVertexStorageBuffers(pass, 0, buffers.data(), (Uint32)buffers.size());
  } else {
    SDL_BindGPUFragmentStorageBuffers(pass, 0, buffers.data(), (Uint32)buffers.size());
  }
}

void bind_storage_textures(SDL_GPURenderPass *pass, const wgsl::StageCode &code,
                           const std::map<int, BindGroupSlot> &groups, bool vertex) {
  std::vector<SDL_GPUTexture *> textures;
  for (const wgsl::Binding &binding : code.storage_textures) {
    int dynamic = 0;
    const BindGroupEntry *entry = bound_entry(groups, binding.group, binding.binding, &dynamic);
    textures.push_back(entry != nullptr ? view_texture(entry->view) : nullptr);
  }
  if (textures.empty()) return;
  if (vertex) {
    SDL_BindGPUVertexStorageTextures(pass, 0, textures.data(), (Uint32)textures.size());
  } else {
    SDL_BindGPUFragmentStorageTextures(pass, 0, textures.data(), (Uint32)textures.size());
  }
}

void push_uniforms(SDL_GPUCommandBuffer *command, const wgsl::StageCode &code,
                   const std::map<int, BindGroupSlot> &groups, bool vertex) {
  for (size_t i = 0; i < code.uniform_buffers.size(); i++) {
    int dynamic = 0;
    const BindGroupEntry *entry = bound_entry(groups, code.uniform_buffers[i].group,
                                              code.uniform_buffers[i].binding, &dynamic);
    if (entry == nullptr) continue;
    std::shared_ptr<Object> object = get_object(entry->buffer, Kind::Buffer);
    if (object == nullptr) continue;
    Buffer *buffer = static_cast<Buffer *>(object.get());
    if (buffer->shadow.empty()) continue;
    const uint64_t offset = entry->offset + (uint64_t)dynamic;
    if (offset >= buffer->shadow.size()) continue;
    uint64_t size = entry->size != 0 ? entry->size : buffer->shadow.size() - offset;
    if (offset + size > buffer->shadow.size()) size = buffer->shadow.size() - offset;
    if (size == 0) continue;
    if (vertex) {
      SDL_PushGPUVertexUniformData(command, (Uint32)i, buffer->shadow.data() + offset, (Uint32)size);
    } else {
      SDL_PushGPUFragmentUniformData(command, (Uint32)i, buffer->shadow.data() + offset,
                                     (Uint32)size);
    }
  }
}

}  // namespace

std::shared_ptr<BindGroup> bound_group(const std::map<int, BindGroupSlot> &groups, int index) {
  auto found = groups.find(index);
  if (found == groups.end()) return nullptr;
  std::shared_ptr<Object> object = get_object(found->second.group, Kind::BindGroup);
  if (object == nullptr) return nullptr;
  return std::static_pointer_cast<BindGroup>(object);
}

const BindGroupEntry *bound_entry(const std::map<int, BindGroupSlot> &groups, int group,
                                  int binding, int *dynamic_offset) {
  if (dynamic_offset != nullptr) *dynamic_offset = 0;
  auto found = groups.find(group);
  if (found == groups.end()) return nullptr;
  std::shared_ptr<BindGroup> bind_group = bound_group(groups, group);
  if (bind_group == nullptr) return nullptr;
  const BindGroupEntry *entry = bind_group->find(binding);
  if (entry != nullptr && dynamic_offset != nullptr) {
    /* Dynamic offsets are listed in the order of the layout's dynamic entries;
     * the common case is one offset per group, which is what this maps. */
    if (!found->second.dynamic_offsets.empty()) {
      *dynamic_offset = (int)found->second.dynamic_offsets[0];
    }
  }
  return entry;
}

SDL_GPUTexture *view_texture(uint32_t view_id) {
  std::shared_ptr<Object> object = get_object(view_id, Kind::TextureView);
  if (object == nullptr) return nullptr;
  return texture_of(static_cast<TextureView *>(object.get())->texture);
}

SDL_GPUTexture *fallback_texture(Device &device) {
  if (device.fallback_texture != nullptr) return device.fallback_texture;
  SDL_GPUTextureCreateInfo info = {};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = 1;
  info.height = 1;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  device.fallback_texture = SDL_CreateGPUTexture(gpu_device(), &info);
  return device.fallback_texture;
}

SDL_GPUSampler *fallback_sampler(Device &device) {
  if (device.fallback_sampler != nullptr) return device.fallback_sampler;
  SDL_GPUSamplerCreateInfo info = {};
  info.min_filter = SDL_GPU_FILTER_NEAREST;
  info.mag_filter = SDL_GPU_FILTER_NEAREST;
  info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  device.fallback_sampler = SDL_CreateGPUSampler(gpu_device(), &info);
  return device.fallback_sampler;
}

bool bind_render_resources(RenderPassEncoder &pass, RenderPipeline &pipeline,
                           SDL_GPUCommandBuffer *command) {
  Device *device = device_of_pass(pass.device_id);
  if (command == nullptr || pass.pass == nullptr) return false;
  bind_samplers(pass.pass, pipeline.vertex.code, pass.groups, true, device);
  bind_storage_buffers(pass.pass, pipeline.vertex.code, pass.groups, true);
  bind_storage_textures(pass.pass, pipeline.vertex.code, pass.groups, true);
  push_uniforms(command, pipeline.vertex.code, pass.groups, true);
  if (pipeline.has_fragment) {
    bind_samplers(pass.pass, pipeline.fragment.code, pass.groups, false, device);
    bind_storage_buffers(pass.pass, pipeline.fragment.code, pass.groups, false);
    bind_storage_textures(pass.pass, pipeline.fragment.code, pass.groups, false);
    push_uniforms(command, pipeline.fragment.code, pass.groups, false);
  }
  return true;
}

bool bind_compute_resources(ComputePassEncoder &pass, ComputePipeline &pipeline,
                            SDL_GPUCommandBuffer *command) {
  Device *device = device_of_pass(pass.device_id);
  if (command == nullptr) return false;
  if (!pass.begun) {
    /* SDL binds read-write storage resources when the pass begins, so they are
     * collected from the bind groups set before the first dispatch. */
    std::vector<SDL_GPUStorageTextureReadWriteBinding> textures;
    for (const wgsl::Binding &binding : pipeline.stage.code.write_storage_textures) {
      const BindGroupEntry *entry = bound_entry(pass.groups, binding.group, binding.binding, nullptr);
      SDL_GPUStorageTextureReadWriteBinding texture = {};
      texture.texture = entry != nullptr ? view_texture(entry->view) : nullptr;
      /* Keep the previous contents: a WebGPU storage texture may be read too. */
      texture.cycle = false;
      textures.push_back(texture);
    }
    std::vector<SDL_GPUStorageBufferReadWriteBinding> buffers;
    for (const wgsl::Binding &binding : pipeline.stage.code.write_storage_buffers) {
      const BindGroupEntry *entry = bound_entry(pass.groups, binding.group, binding.binding, nullptr);
      SDL_GPUStorageBufferReadWriteBinding buffer = {};
      buffer.buffer = entry != nullptr ? buffer_of(entry->buffer) : nullptr;
      /* Keep the previous contents: the shader may read before writing. */
      buffer.cycle = false;
      buffers.push_back(buffer);
    }
    pass.pass = SDL_BeginGPUComputePass(command, textures.empty() ? nullptr : textures.data(),
                                        (Uint32)textures.size(),
                                        buffers.empty() ? nullptr : buffers.data(),
                                        (Uint32)buffers.size());
    pass.begun = true;
  }
  if (pass.pass == nullptr) return false;

  std::vector<SDL_GPUTextureSamplerBinding> samplers;
  for (const wgsl::TextureSlot &slot : pipeline.stage.code.samplers) {
    SDL_GPUTexture *texture = nullptr;
    SDL_GPUSampler *sampler = nullptr;
    const BindGroupEntry *entry = bound_entry(pass.groups, slot.texture.group, slot.texture.binding,
                                              nullptr);
    if (entry != nullptr) texture = view_texture(entry->view);
    if (slot.has_sampler) {
      const BindGroupEntry *sampler_entry =
          bound_entry(pass.groups, slot.sampler.group, slot.sampler.binding, nullptr);
      if (sampler_entry != nullptr) sampler = sampler_of(sampler_entry->sampler);
    }
    if (texture == nullptr && device != nullptr) texture = fallback_texture(*device);
    if (sampler == nullptr && device != nullptr) sampler = fallback_sampler(*device);
    SDL_GPUTextureSamplerBinding binding = {};
    binding.texture = texture;
    binding.sampler = sampler;
    samplers.push_back(binding);
  }
  if (!samplers.empty()) {
    SDL_BindGPUComputeSamplers(pass.pass, 0, samplers.data(), (Uint32)samplers.size());
  }
  if (!pipeline.stage.code.storage_textures.empty()) {
    std::vector<SDL_GPUTexture *> textures;
    for (const wgsl::Binding &binding : pipeline.stage.code.storage_textures) {
      const BindGroupEntry *entry = bound_entry(pass.groups, binding.group, binding.binding, nullptr);
      textures.push_back(entry != nullptr ? view_texture(entry->view) : nullptr);
    }
    SDL_BindGPUComputeStorageTextures(pass.pass, 0, textures.data(), (Uint32)textures.size());
  }
  if (!pipeline.stage.code.storage_buffers.empty()) {
    std::vector<SDL_GPUBuffer *> buffers;
    for (const wgsl::Binding &binding : pipeline.stage.code.storage_buffers) {
      const BindGroupEntry *entry = bound_entry(pass.groups, binding.group, binding.binding, nullptr);
      buffers.push_back(entry != nullptr ? buffer_of(entry->buffer) : nullptr);
    }
    SDL_BindGPUComputeStorageBuffers(pass.pass, 0, buffers.data(), (Uint32)buffers.size());
  }
  for (size_t i = 0; i < pipeline.stage.code.uniform_buffers.size(); i++) {
    const BindGroupEntry *entry = bound_entry(pass.groups, pipeline.stage.code.uniform_buffers[i].group,
                                              pipeline.stage.code.uniform_buffers[i].binding, nullptr);
    if (entry == nullptr) continue;
    std::shared_ptr<Object> object = get_object(entry->buffer, Kind::Buffer);
    if (object == nullptr) continue;
    Buffer *buffer = static_cast<Buffer *>(object.get());
    if (buffer->shadow.empty()) continue;
    uint64_t offset = entry->offset;
    if (offset >= buffer->shadow.size()) continue;
    uint64_t size = entry->size != 0 ? entry->size : buffer->shadow.size() - offset;
    if (offset + size > buffer->shadow.size()) size = buffer->shadow.size() - offset;
    SDL_PushGPUComputeUniformData(command, (Uint32)i, buffer->shadow.data() + offset, (Uint32)size);
  }
  return true;
}

}  // namespace webgpu
}  // namespace xtgui
