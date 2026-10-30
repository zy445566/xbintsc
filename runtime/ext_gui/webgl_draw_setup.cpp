/*
 * xbintsc GUI engine — prepared WebGL draws.
 *
 * Every draw needs the same three things: the pipeline for the current
 * fixed-function state, the vertex layout of the program's active attributes,
 * and the GPU buffer handles behind them. Building that per draw was the
 * dominant CPU cost of a multi-element scene, so it is built once per
 * (program, layout version, state) and cached in `draw_setups_`.
 *
 * A setup is invalidated by bumping one of the context's state versions:
 * `attribs_version_` for anything that changes the vertex layout (attribute
 * pointers, enables, VAO loads), `gpu_version_` for buffer handle changes, and
 * `framebuffer_version_` for attachment-format changes.
 */

#include "webgl.h"

#include <cstring>
#include <utility>
#include <vector>

#include "gui_internal.h"

namespace xtgui {
namespace {

int components_of_type(const std::string &type) {
  if (!type.empty() && type.back() >= '2' && type.back() <= '4') return type.back() - '0';
  return 1;
}

int matrix_slots(const std::string &type) {
  if (type == "mat2") return 2;
  if (type == "mat3") return 3;
  if (type == "mat4") return 4;
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

/** Content hash of the vertex input state a pipeline bakes in.
 *
 * `WebGLDrawKey` carries `attribs_version_`/`framebuffer_version_`, which are
 * *monotonic counters*: every VAO bind and every framebuffer switch bumps them.
 * A page that binds several VAOs per frame (three.js does, once per geometry)
 * therefore produced a new pipeline key for every draw on every frame, and the
 * per-program pipeline cache never hit — measured at ~250
 * `SDL_CreateGPUGraphicsPipeline` calls per frame, several milliseconds of the
 * frame on its own, with a cache that grew by that many entries every frame.
 * The pipeline only depends on the layout itself, so key it by the layout's
 * content: the same layout stays cached across frames and is shared by every
 * object that uses it. */
uint64_t vertex_layout_hash(const std::vector<SDL_GPUVertexAttribute> &attributes,
                            const std::vector<SDL_GPUVertexBufferDescription> &buffers) {
  uint64_t hash = 1469598103934665603ull;
  auto mix = [&hash](uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
    hash ^= hash >> 29;
  };
  mix(attributes.size());
  for (const SDL_GPUVertexAttribute &attribute : attributes) {
    mix(attribute.location);
    mix(attribute.buffer_slot);
    mix((uint64_t)(uint32_t)attribute.format);
    mix(attribute.offset);
  }
  mix(buffers.size());
  for (const SDL_GPUVertexBufferDescription &buffer : buffers) {
    mix(buffer.slot);
    mix(buffer.pitch);
    mix((uint64_t)(uint32_t)buffer.input_rate);
  }
  return hash;
}

}  // namespace

void WebGLContext::draw_state_hash(uint64_t *first, uint64_t *second) const {
  uint64_t h1 = 1469598103934665603ull;
  uint64_t h2 = 0x9e3779b97f4a7c15ull;
  auto mix = [&h1, &h2](uint64_t value) {
    h1 ^= value;
    h1 *= 1099511628211ull;
    h1 ^= h1 >> 32;
    h2 += value + 0x9e3779b97f4a7c15ull;
    h2 = (h2 << 27) | (h2 >> 37);
    h2 *= 0xbf58476d1ce4e5b9ull;
  };
  auto mix_face = [&mix](const WebGLStencilFace &face) {
    /* `ref` is a per-pass dynamic value, not part of the pipeline. */
    mix((uint64_t)(uint32_t)face.func);
    mix(face.value_mask);
    mix(face.write_mask);
    mix((uint64_t)(uint32_t)face.fail);
    mix((uint64_t)(uint32_t)face.zfail);
    mix((uint64_t)(uint32_t)face.zpass);
  };

  mix(state_.blend ? 1u : 0u);
  mix((uint64_t)(uint32_t)state_.blend_src_rgb);
  mix((uint64_t)(uint32_t)state_.blend_dst_rgb);
  mix((uint64_t)(uint32_t)state_.blend_src_alpha);
  mix((uint64_t)(uint32_t)state_.blend_dst_alpha);
  mix((uint64_t)(uint32_t)state_.blend_eq_rgb);
  mix((uint64_t)(uint32_t)state_.blend_eq_alpha);
  mix(state_.depth_test ? 1u : 0u);
  mix(state_.depth_write ? 1u : 0u);
  mix((uint64_t)(uint32_t)state_.depth_func);
  mix(state_.stencil ? 1u : 0u);
  mix_face(state_.stencil_front);
  mix_face(state_.stencil_back);
  mix(state_.cull ? 1u : 0u);
  mix((uint64_t)(uint32_t)state_.cull_face);
  mix((uint64_t)(uint32_t)state_.front_face);
  for (bool channel : state_.color_mask) mix(channel ? 1u : 0u);
  mix((uint64_t)(uint32_t)state_.color_target_count);
  for (int i = 0; i < state_.color_target_count; i++) {
    mix((uint64_t)(uint32_t)state_.color_formats[i]);
  }
  mix((uint64_t)(uint32_t)state_.depth_format);

  *first = h1;
  *second = h2;
}

void WebGLContext::clear_draw_cache() {
  draw_setups_.clear();
  last_setup_ = nullptr;
  last_pipeline_ = nullptr;
  last_setup_pass_ = 0;
}

const WebGLDrawSetup *WebGLContext::prepare_draw(const WebGLDrawKey &key) {
  auto found = draw_setups_.find(key);
  if (found != draw_setups_.end()) return &found->second;
  WebGLProgram *program_ = program(key.program);
  if (program_ == nullptr) {
    set_error(GL_INVALID_OPERATION);
    return nullptr;
  }
  /* A page that cycles through many states would otherwise grow the table
   * without bound; clearing only costs one rebuild per live state. */
  if (draw_setups_.size() >= 256) clear_draw_cache();

  WebGLDrawSetup setup;
  setup.attributes.reserve(program_->draw_attribs.size());
  setup.buffers.reserve(program_->draw_attribs.size());
  setup.bindings.reserve(program_->draw_attribs.size());

  for (const WebGLDrawAttribPlan &plan : program_->draw_attribs) {
    if (plan.location < 0 || plan.location >= kWebGLMaxAttribs) continue;
    WebGLAttribState &attrib = attribs_[plan.location];
    const int slots = matrix_slots(plan.type);
    const int type_components = components_of_type(plan.type);
    const int components = slots > 1 ? slots : (type_components > 4 ? 4 : type_components);
    const uint32_t slot = (uint32_t)setup.buffers.size();

    SDL_GPUVertexBufferDescription description;
    memset(&description, 0, sizeof(description));
    description.slot = slot;
    description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUBufferBinding binding;
    memset(&binding, 0, sizeof(binding));

    if (!attrib.enabled) {
      /* A disabled attribute is a constant read from a persistent 16-byte
       * buffer, not a fresh GPU allocation per draw. */
      SDL_GPUBuffer *constant = constant_attrib_buffer(plan.location);
      if (constant == nullptr) return nullptr;
      binding.buffer = constant;
      description.pitch = 0;
      for (int column = 0; column < slots; column++) {
        SDL_GPUVertexAttribute attribute;
        memset(&attribute, 0, sizeof(attribute));
        attribute.location = (Uint32)(plan.location + column);
        attribute.buffer_slot = slot;
        attribute.format = float_format(components);
        attribute.offset = 0;
        setup.attributes.push_back(attribute);
      }
      setup.buffers.push_back(description);
      setup.bindings.push_back(binding);
      continue;
    }

    WebGLBuffer *buffer_ = buffer(attrib.buffer);
    if (buffer_ == nullptr || buffer_->data.empty()) {
      set_error(GL_INVALID_OPERATION);
      return nullptr;
    }
    SDL_GPUVertexElementFormat format = attrib.integer
                                            ? int_format_of(attrib.size, attrib.type)
                                            : format_of(attrib.size, attrib.type, attrib.normalized);
    if (format == SDL_GPU_VERTEXELEMENTFORMAT_INVALID) {
      set_error(GL_INVALID_OPERATION);
      return nullptr;
    }
    description.input_rate = attrib.divisor > 0 ? SDL_GPU_VERTEXINPUTRATE_INSTANCE
                                                : SDL_GPU_VERTEXINPUTRATE_VERTEX;
    const int stride =
        attrib.stride > 0 ? attrib.stride : components * type_size(attrib.type) * slots;
    description.pitch = (Uint32)stride;
    SDL_GPUBuffer *gpu = ensure_buffer_gpu(buffer_);
    if (gpu == nullptr) return nullptr;
    binding.buffer = gpu;
    for (int column = 0; column < slots; column++) {
      SDL_GPUVertexAttribute attribute;
      memset(&attribute, 0, sizeof(attribute));
      attribute.location = (Uint32)(plan.location + column);
      attribute.buffer_slot = slot;
      attribute.format = format;
      attribute.offset = (Uint32)(attrib.offset + column * components * type_size(attrib.type));
      setup.attributes.push_back(attribute);
    }
    setup.buffers.push_back(description);
    setup.bindings.push_back(binding);
  }

  if (key.index_size > 0 && key.element_buffer != 0) {
    WebGLBuffer *element = buffer(key.element_buffer);
    if (element == nullptr || element->data.empty()) {
      set_error(GL_INVALID_OPERATION);
      return nullptr;
    }
    setup.index_buffer = ensure_buffer_gpu(element);
    if (setup.index_buffer == nullptr) return nullptr;
  }

  /* The pipeline depends on the vertex layout and fixed-function state, not on
   * which GPU buffer happens to hold the data (a buffer can be recreated after
   * a grow) nor on the element-buffer binding. Normalise those fields out so a
   * dynamic buffer does not create a fresh pipeline. The monotonic version
   * counters are replaced by the layout's content hash (and zero): they change
   * on every VAO bind / framebuffer switch without changing the pipeline, so
   * keying on them re-created every pipeline once per frame. */
  WebGLDrawKey pipeline_key = key;
  pipeline_key.gpu_version = 0;
  pipeline_key.element_buffer = 0;
  pipeline_key.attribs_version = vertex_layout_hash(setup.attributes, setup.buffers);
  pipeline_key.framebuffer_version = 0;
  auto cached = program_->pipeline_cache.find(pipeline_key);
  if (cached != program_->pipeline_cache.end()) {
    setup.pipeline = cached->second;
  } else {
    setup.pipeline = create_pipeline(program_, key.mode, (int)setup.attributes.size(),
                                     (int)setup.buffers.size(), setup.attributes.data(),
                                     setup.buffers.data());
    if (setup.pipeline == nullptr) return nullptr;
    program_->pipeline_cache[pipeline_key] = setup.pipeline;
    program_->pipelines.push_back(setup.pipeline);
  }

  auto inserted = draw_setups_.emplace(key, std::move(setup));
  return &inserted.first->second;
}

}  // namespace xtgui
