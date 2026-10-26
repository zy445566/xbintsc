/*
 * xbintsc GUI engine — WebGL draw calls.
 *
 * SDL_GPU has no dynamic state for blend/depth/rasterisation: those live in a
 * `SDL_GPUGraphicsPipeline`. WebGL changes them with `glEnable`/`glDepthFunc`,
 * so a pipeline is built lazily per (program, vertex layout, the subset of
 * fixed-function state that matters) and cached on the program.
 *
 * A matrix attribute consumes several consecutive vertex locations; the
 * pipeline spells out one `SDL_GPUVertexAttribute` per column, all reading the
 * same vertex buffer binding.
 */

#include "webgl.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "gui_internal.h"

namespace xtgui {
namespace {

int type_size(int type) {
  switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
      return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
      return 2;
    default:
      return 4;
  }
}

int components_of_type(const std::string &type) {
  if (!type.empty() && type.back() >= '2' && type.back() <= '4') return type.back() - '0';
  return 1;
}

SDL_GPUVertexElementFormat float_format(int size) {
  switch (size) {
    case 1:
      return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    case 2:
      return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case 3:
      return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    default:
      return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
  }
}

SDL_GPUVertexElementFormat format_of(int size, int type, bool normalized) {
  if (size < 1 || size > 4) return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
  if (type == GL_FLOAT) return float_format(size);
  if (type == GL_UNSIGNED_BYTE || type == GL_BYTE) {
    const bool is_byte = type == GL_BYTE;
    if (size == 2) {
      return normalized ? (is_byte ? SDL_GPU_VERTEXELEMENTFORMAT_BYTE2_NORM
                                   : SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2_NORM)
                        : (is_byte ? SDL_GPU_VERTEXELEMENTFORMAT_BYTE2
                                   : SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2);
    }
    if (size == 4) {
      return normalized ? (is_byte ? SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM
                                   : SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM)
                        : (is_byte ? SDL_GPU_VERTEXELEMENTFORMAT_BYTE4
                                   : SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4);
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
  }
  if (type == GL_UNSIGNED_SHORT || type == GL_SHORT) {
    const bool is_short = type == GL_SHORT;
    if (size == 2) {
      return normalized ? (is_short ? SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM
                                    : SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM)
                        : (is_short ? SDL_GPU_VERTEXELEMENTFORMAT_SHORT2
                                    : SDL_GPU_VERTEXELEMENTFORMAT_USHORT2);
    }
    if (size == 4) {
      return normalized ? (is_short ? SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM
                                    : SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM)
                        : (is_short ? SDL_GPU_VERTEXELEMENTFORMAT_SHORT4
                                    : SDL_GPU_VERTEXELEMENTFORMAT_USHORT4);
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
  }
  return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
}

/** `vertexAttribIPointer` formats: integer attributes keep their integer type
 * in the shader (`in ivec3 …`). SDL has no 1- or 3-component byte/short
 * integer formats, so only the sizes it can express are accepted. */
SDL_GPUVertexElementFormat int_format_of(int size, int type) {
  if (size < 1 || size > 4) return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
  if (type == GL_INT) {
    switch (size) {
      case 1:
        return SDL_GPU_VERTEXELEMENTFORMAT_INT;
      case 2:
        return SDL_GPU_VERTEXELEMENTFORMAT_INT2;
      case 3:
        return SDL_GPU_VERTEXELEMENTFORMAT_INT3;
      default:
        return SDL_GPU_VERTEXELEMENTFORMAT_INT4;
    }
  }
  if (type == GL_UNSIGNED_INT) {
    switch (size) {
      case 1:
        return SDL_GPU_VERTEXELEMENTFORMAT_UINT;
      case 2:
        return SDL_GPU_VERTEXELEMENTFORMAT_UINT2;
      case 3:
        return SDL_GPU_VERTEXELEMENTFORMAT_UINT3;
      default:
        return SDL_GPU_VERTEXELEMENTFORMAT_UINT4;
    }
  }
  if (type == GL_SHORT && (size == 2 || size == 4)) {
    return size == 2 ? SDL_GPU_VERTEXELEMENTFORMAT_SHORT2 : SDL_GPU_VERTEXELEMENTFORMAT_SHORT4;
  }
  if (type == GL_UNSIGNED_SHORT && (size == 2 || size == 4)) {
    return size == 2 ? SDL_GPU_VERTEXELEMENTFORMAT_USHORT2
                     : SDL_GPU_VERTEXELEMENTFORMAT_USHORT4;
  }
  if (type == GL_BYTE && (size == 2 || size == 4)) {
    return size == 2 ? SDL_GPU_VERTEXELEMENTFORMAT_BYTE2 : SDL_GPU_VERTEXELEMENTFORMAT_BYTE4;
  }
  if (type == GL_UNSIGNED_BYTE && (size == 2 || size == 4)) {
    return size == 2 ? SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2 : SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
  }
  return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
}

/** A cache key covering everything (other than the program) that a
 * `SDL_GPUGraphicsPipeline` bakes in. */
std::string pipeline_key(int mode, int index_size,
                         const std::vector<SDL_GPUVertexAttribute> &attrs,
                         const std::vector<SDL_GPUVertexBufferDescription> &buffers,
                         const WebGLDrawState &state) {
  std::string key = std::to_string(mode) + "/" + std::to_string(index_size);
  key += "/f" + std::to_string(state.color_format) + ":" + std::to_string(state.depth_format);
  for (const SDL_GPUVertexAttribute &attr : attrs) {
    key += "/" + std::to_string(attr.location) + ":" + std::to_string(attr.buffer_slot) + ":" +
           std::to_string((int)attr.format) + ":" + std::to_string(attr.offset);
  }
  for (const SDL_GPUVertexBufferDescription &buffer : buffers) {
    key += "|" + std::to_string(buffer.slot) + ":" + std::to_string(buffer.pitch) + ":" +
           std::to_string((int)buffer.input_rate);
  }
  char state_key[256];
  snprintf(state_key, sizeof(state_key), "|%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
           (int)state.blend, state.blend_src_rgb, state.blend_dst_rgb, state.blend_src_alpha,
           state.blend_dst_alpha, state.blend_eq_rgb, state.blend_eq_alpha, (int)state.depth_test,
           (int)state.depth_write, state.depth_func, (int)state.cull, state.cull_face,
           state.front_face, (int)state.color_mask[0]);
  key += state_key;
  key += std::to_string((int)state.color_mask[1]);
  key += std::to_string((int)state.color_mask[2]);
  key += std::to_string((int)state.color_mask[3]);
  char stencil_key[224];
  snprintf(stencil_key, sizeof(stencil_key), "|s%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%u,%u,%u",
           (int)state.stencil, state.stencil_front.func, state.stencil_front.fail,
           state.stencil_front.zfail, state.stencil_front.zpass,
           (unsigned)state.stencil_front.write_mask, state.stencil_back.func,
           state.stencil_back.fail, state.stencil_back.zfail, state.stencil_back.zpass,
           (unsigned)state.stencil_back.write_mask,
           (unsigned)state.stencil_front.value_mask,
           (unsigned)state.stencil_front.write_mask);
  key += stencil_key;
  return key;
}

}  // namespace

void WebGLContext::draw_arrays(int mode, int first, int count) {
  if (count < 0) {
    set_error(GL_INVALID_VALUE);
    return;
  }
  draw(mode, first, count, 0, 0, 1);
}

void WebGLContext::draw_elements(int mode, int count, int type, int offset) {
  if (count < 0) {
    set_error(GL_INVALID_VALUE);
    return;
  }
  if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT && type != GL_UNSIGNED_BYTE) {
    set_error(GL_INVALID_ENUM);
    return;
  }
  draw(mode, 0, count, type, offset, 1);
}

void WebGLContext::draw_arrays_instanced(int mode, int first, int count, int instances) {
  if (count < 0 || instances < 0) {
    set_error(GL_INVALID_VALUE);
    return;
  }
  draw(mode, first, count, 0, 0, instances > 0 ? instances : 1);
}

void WebGLContext::draw_elements_instanced(int mode, int count, int type, int offset,
                                           int instances) {
  if (count < 0 || instances < 0) {
    set_error(GL_INVALID_VALUE);
    return;
  }
  if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT && type != GL_UNSIGNED_BYTE) {
    set_error(GL_INVALID_ENUM);
    return;
  }
  draw(mode, 0, count, type, offset, instances > 0 ? instances : 1);
}

void WebGLContext::draw_range_elements(int mode, int start, int end, int count, int type,
                                       int offset) {
  (void)start;
  (void)end;
  draw_elements(mode, count, type, offset);
}

void WebGLContext::draw(int mode, int first, int count, int index_type, int offset,
                        int instances) {
  if (count <= 0) return;
  WebGLProgram *program_ = program(current_program_);
  if (program_ == nullptr || !program_->linked || program_->vs_shader == nullptr) {
    set_error(GL_INVALID_OPERATION);
    return;
  }
  if (mode != GL_POINTS && mode != GL_LINES && mode != GL_LINE_STRIP && mode != GL_TRIANGLES &&
      mode != GL_TRIANGLE_STRIP && mode != GL_TRIANGLE_FAN && mode != GL_LINE_LOOP) {
    set_error(GL_INVALID_ENUM);
    return;
  }
  ensure_fallback();
  end_pass();
  ensure_command();

  /* Expand source indices when the draw needs a primitive SDL_GPU lacks or a
   * byte-sized index type. */
  std::vector<uint32_t> expanded;
  SDL_GPUBuffer *index_gpu = nullptr;
  int index_size = 0;
  int first_index = 0;
  int draw_mode = mode;
  const bool needs_expand =
      mode == GL_TRIANGLE_FAN || mode == GL_LINE_LOOP || index_type == GL_UNSIGNED_BYTE;
  if (needs_expand) {
    std::vector<uint32_t> source;
    source.reserve((size_t)count);
    if (index_type != 0) {
      WebGLBuffer *element = buffer(bound_element_);
      if (element == nullptr || element->data.empty()) {
        set_error(GL_INVALID_OPERATION);
        return;
      }
      const int element_size = type_size(index_type);
      for (int i = 0; i < count; i++) {
        const size_t at = (size_t)offset + (size_t)i * element_size;
        if (at + element_size > element->data.size()) {
          source.push_back(0);
          continue;
        }
        uint32_t value = element->data[at];
        if (element_size >= 2) value |= (uint32_t)element->data[at + 1] << 8;
        if (element_size >= 4) {
          value |= (uint32_t)element->data[at + 2] << 16;
          value |= (uint32_t)element->data[at + 3] << 24;
        }
        source.push_back(value);
      }
    } else {
      for (int i = 0; i < count; i++) source.push_back((uint32_t)(first + i));
    }
    if (mode == GL_TRIANGLE_FAN) {
      for (size_t i = 1; i + 1 < source.size(); i++) {
        expanded.push_back(source[0]);
        expanded.push_back(source[i]);
        expanded.push_back(source[i + 1]);
      }
      draw_mode = GL_TRIANGLES;
    } else if (mode == GL_LINE_LOOP) {
      for (size_t i = 0; i < source.size(); i++) {
        expanded.push_back(source[i]);
        expanded.push_back(source[(i + 1) % source.size()]);
      }
      draw_mode = GL_LINES;
    } else {
      expanded = source;
      draw_mode = mode;
    }
    index_size = 4;
    if (!expanded.empty()) {
      index_gpu = upload_temp_buffer(expanded.data(),
                                     (uint32_t)(expanded.size() * sizeof(uint32_t)),
                                     SDL_GPU_BUFFERUSAGE_INDEX);
      if (index_gpu == nullptr) return;
    }
  } else if (index_type != 0) {
    WebGLBuffer *element = buffer(bound_element_);
    if (element == nullptr || element->data.empty()) {
      set_error(GL_INVALID_OPERATION);
      return;
    }
    index_gpu = ensure_buffer_gpu(element);
    if (index_gpu == nullptr) return;
    index_size = type_size(index_type) == 4 ? 4 : 2;
    first_index = offset / index_size;
  }

  WebGLFramebuffer *framebuffer_ =
      bound_framebuffer_ != 0 ? framebuffer(bound_framebuffer_) : nullptr;
  const int target_width = framebuffer_ != nullptr ? framebuffer_->width : width_;
  const int target_height = framebuffer_ != nullptr ? framebuffer_->height : height_;
  /* A pipeline bakes in the attachment formats; float colour targets and depth
   * textures therefore need their own pipeline variants. */
  state_.color_format = target_color_format();
  state_.depth_format = target_depth_format();

  std::vector<SDL_GPUVertexAttribute> attributes;
  std::vector<SDL_GPUVertexBufferDescription> buffers;
  std::vector<SDL_GPUBufferBinding> bindings;

  for (const GlslUniformDecl &decl : program_->attribs) {
    auto location_it = program_->attrib_locations.find(decl.name);
    if (location_it == program_->attrib_locations.end()) continue;
    const int base_location = location_it->second;
    if (base_location < 0 || base_location >= kWebGLMaxAttribs) continue;
    WebGLAttribState &attrib = attribs_[base_location];
    const int slots = decl.type == "mat2" ? 2 : (decl.type == "mat3" ? 3 : (decl.type == "mat4" ? 4 : 1));
    const int components = slots > 1 ? slots : (components_of_type(decl.type) > 4 ? 4
                                                                                 : components_of_type(decl.type));
    const uint32_t slot = (uint32_t)buffers.size();
    SDL_GPUVertexBufferDescription description;
    memset(&description, 0, sizeof(description));
    description.slot = slot;
    description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUBufferBinding binding;
    memset(&binding, 0, sizeof(binding));
    if (!attrib.enabled) {
      float values[4] = {attrib.current[0], attrib.current[1], attrib.current[2], attrib.current[3]};
      binding.buffer = upload_temp_buffer(values, sizeof(values), SDL_GPU_BUFFERUSAGE_VERTEX);
      description.pitch = 0;
      if (binding.buffer == nullptr) continue;
      for (int column = 0; column < slots; column++) {
        SDL_GPUVertexAttribute attribute;
        memset(&attribute, 0, sizeof(attribute));
        attribute.location = (Uint32)(base_location + column);
        attribute.buffer_slot = slot;
        attribute.format = float_format(components);
        attribute.offset = 0;
        attributes.push_back(attribute);
      }
      buffers.push_back(description);
      bindings.push_back(binding);
      continue;
    }
    WebGLBuffer *buffer_ = buffer(attrib.buffer);
    if (buffer_ == nullptr || buffer_->data.empty()) {
      set_error(GL_INVALID_OPERATION);
      return;
    }
    SDL_GPUVertexElementFormat format = attrib.integer
                                            ? int_format_of(attrib.size, attrib.type)
                                            : format_of(attrib.size, attrib.type, attrib.normalized);
    if (format == SDL_GPU_VERTEXELEMENTFORMAT_INVALID) {
      set_error(GL_INVALID_OPERATION);
      return;
    }
    description.input_rate = attrib.divisor > 0 ? SDL_GPU_VERTEXINPUTRATE_INSTANCE
                                                : SDL_GPU_VERTEXINPUTRATE_VERTEX;
    const int stride =
        attrib.stride > 0 ? attrib.stride : components * type_size(attrib.type) * slots;
    description.pitch = (Uint32)stride;
    SDL_GPUBuffer *gpu = ensure_buffer_gpu(buffer_);
    if (gpu == nullptr) return;
    binding.buffer = gpu;
    binding.offset = 0;
    for (int column = 0; column < slots; column++) {
      SDL_GPUVertexAttribute attribute;
      memset(&attribute, 0, sizeof(attribute));
      attribute.location = (Uint32)(base_location + column);
      attribute.buffer_slot = slot;
      attribute.format = format;
      attribute.offset = (Uint32)(attrib.offset + column * components * type_size(attrib.type));
      attributes.push_back(attribute);
    }
    buffers.push_back(description);
    bindings.push_back(binding);
  }

  begin_pass(false, false, false);
  if (pass_ == nullptr) return;

  SDL_GPUViewport viewport;
  memset(&viewport, 0, sizeof(viewport));
  viewport.x = (float)viewport_[0];
  viewport.y = (float)(target_height - (viewport_[1] + viewport_[3]));
  viewport.w = (float)std::max(1, viewport_[2]);
  viewport.h = (float)std::max(1, viewport_[3]);
  viewport.min_depth = 0.0f;
  viewport.max_depth = 1.0f;
  SDL_SetGPUViewport(pass_, &viewport);

  SDL_Rect scissor;
  scissor.x = 0;
  scissor.y = 0;
  scissor.w = target_width;
  scissor.h = target_height;
  if (scissor_test_ && scissor_box_[2] > 0 && scissor_box_[3] > 0) {
    scissor.x = scissor_box_[0];
    scissor.y = target_height - (scissor_box_[1] + scissor_box_[3]);
    scissor.w = scissor_box_[2];
    scissor.h = scissor_box_[3];
  }
  if (scissor.x < 0) {
    scissor.w += scissor.x;
    scissor.x = 0;
  }
  if (scissor.y < 0) {
    scissor.h += scissor.y;
    scissor.y = 0;
  }
  if (scissor.x + scissor.w > target_width) scissor.w = target_width - scissor.x;
  if (scissor.y + scissor.h > target_height) scissor.h = target_height - scissor.y;
  if (scissor.w < 0) scissor.w = 0;
  if (scissor.h < 0) scissor.h = 0;
  SDL_SetGPUScissor(pass_, &scissor);

  const std::string key = pipeline_key(draw_mode, index_size, attributes, buffers, state_);
  SDL_GPUGraphicsPipeline *pipeline = nullptr;
  auto cached = program_->pipeline_cache.find(key);
  if (cached != program_->pipeline_cache.end()) {
    pipeline = cached->second;
  } else {
    pipeline = create_pipeline(program_, draw_mode, index_size, (int)attributes.size(),
                               (int)buffers.size(), attributes.data(), buffers.data(), key);
  }
  if (pipeline == nullptr) return;

  SDL_BindGPUGraphicsPipeline(pass_, pipeline);
  if (state_.stencil) {
    SDL_SetGPUStencilReference(pass_, (Uint8)(stencil_reference() & 0xFF));
  }
  if (state_.blend) {
    SDL_FColor constants;
    constants.r = blend_color_[0];
    constants.g = blend_color_[1];
    constants.b = blend_color_[2];
    constants.a = blend_color_[3];
    SDL_SetGPUBlendConstants(pass_, constants);
  }
  if (!bindings.empty()) {
    SDL_BindGPUVertexBuffers(pass_, 0, bindings.data(), (Uint32)bindings.size());
  }
  if (index_size > 0) {
    SDL_GPUBufferBinding binding;
    binding.buffer = index_gpu;
    binding.offset = 0;
    SDL_BindGPUIndexBuffer(pass_, &binding,
                           index_size == 4 ? SDL_GPU_INDEXELEMENTSIZE_32BIT
                                           : SDL_GPU_INDEXELEMENTSIZE_16BIT);
  }
  if (program_->vs_has_block && program_->block_size > 0) {
    SDL_PushGPUVertexUniformData(cmd_, 0, program_->block.data(), (Uint32)program_->block.size());
  }
  if (program_->fs_has_block && program_->block_size > 0) {
    SDL_PushGPUFragmentUniformData(cmd_, 0, program_->block.data(), (Uint32)program_->block.size());
  }

  auto bind_samplers = [&](const std::vector<int> &stage, bool vertex_stage) {
    if (stage.empty()) return;
    std::vector<SDL_GPUTextureSamplerBinding> sampler_bindings;
    sampler_bindings.reserve(stage.size());
    for (int uniform_index : stage) {
      const WebGLUniform &uniform = program_->uniforms[(size_t)uniform_index];
      const int unit =
          uniform.unit >= 0 && uniform.unit < kWebGLMaxTextureUnits ? uniform.unit : 0;
      uint32_t texture_id = bound_textures_[unit];
      if (uniform.layout.gl_type == GL_SAMPLER_CUBE) {
        texture_id = bound_cube_textures_[unit];
      } else if (uniform.layout.gl_type == GL_SAMPLER_3D) {
        texture_id = bound_3d_textures_[unit];
      } else if (uniform.layout.gl_type == GL_SAMPLER_2D_ARRAY) {
        texture_id = bound_array_textures_[unit];
      }
      WebGLTexture *sampled = texture(texture_id);
      SDL_GPUTexture *gpu = sampled != nullptr ? ensure_texture_gpu(sampled, sampled->as_target)
                                               : nullptr;
      WebGLSampler *sampler_object = this->sampler(bound_samplers_[unit]);
      SDL_GPUSampler *sampler = sampler_object != nullptr
                                    ? sampler_gpu(sampler_object)
                                    : (sampled != nullptr ? sampler_for(sampled) : nullptr);
      if (gpu == nullptr || sampler == nullptr) {
        gpu = fallback_texture_;
        sampler = fallback_sampler_;
      }
      SDL_GPUTextureSamplerBinding texture_binding;
      texture_binding.texture = gpu;
      texture_binding.sampler = sampler;
      sampler_bindings.push_back(texture_binding);
    }
    if (vertex_stage) {
      SDL_BindGPUVertexSamplers(pass_, 0, sampler_bindings.data(),
                                (Uint32)sampler_bindings.size());
    } else {
      SDL_BindGPUFragmentSamplers(pass_, 0, sampler_bindings.data(),
                                  (Uint32)sampler_bindings.size());
    }
  };
  bind_samplers(program_->vs_sampler_uniforms, true);
  bind_samplers(program_->fs_sampler_uniforms, false);

  if (index_size > 0) {
    SDL_DrawGPUIndexedPrimitives(pass_, (Uint32)count, (Uint32)instances, (Uint32)first_index, 0,
                                 0);
  } else {
    SDL_DrawGPUPrimitives(pass_, (Uint32)count, (Uint32)instances, (Uint32)first, 0);
  }
}

}  // namespace xtgui