/*
 * xbintsc GUI engine — WebGL draw calls.
 *
 * SDL_GPU has no dynamic state for blend/depth/rasterisation: those live in a
 * `SDL_GPUGraphicsPipeline`. WebGL changes them with `glEnable`/`glDepthFunc`,
 * so a pipeline is built lazily per (program, vertex layout, state) and cached.
 *
 * A draw is mostly a cache lookup: `prepare_draw` returns the pipeline plus the
 * vertex-layout bindings for the current state, and a repeated draw in the same
 * render pass is one pipeline bind (when it changed) and one `SDL_Draw*` call.
 * Everything that needs a GPU upload (buffer/texture data, an expanded index
 * list, a constant attribute) records that upload *before* `begin_pass`, so an
 * unchanged sequence of draws never ends the pass it is recording into.
 */

#include "webgl.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "gui_internal.h"
#include "perf.h"

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
  g_perf_frame.webglDraws += 1;
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
  ensure_command();
  if (cmd_ == nullptr) return;

  /* Expand source indices when the draw needs a primitive SDL_GPU lacks or a
   * byte-sized index type. These are per-draw temporary buffers: they change
   * the index buffer, not the pipeline, so they stay out of the setup cache. */
  std::vector<uint32_t> expanded;
  SDL_GPUBuffer *expanded_gpu = nullptr;
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
      expanded_gpu = upload_temp_buffer(expanded.data(),
                                        (uint32_t)(expanded.size() * sizeof(uint32_t)),
                                        SDL_GPU_BUFFERUSAGE_INDEX);
      if (expanded_gpu == nullptr) return;
    }
  } else if (index_type != 0) {
    WebGLBuffer *element = buffer(bound_element_);
    if (element == nullptr || element->data.empty()) {
      set_error(GL_INVALID_OPERATION);
      return;
    }
    index_size = type_size(index_type) == 4 ? 4 : 2;
    first_index = offset / index_size;
  }

  WebGLFramebuffer *framebuffer_ =
      bound_framebuffer_ != 0 ? framebuffer(bound_framebuffer_) : nullptr;
  const int target_width = framebuffer_ != nullptr ? framebuffer_->width : width_;
  const int target_height = framebuffer_ != nullptr ? framebuffer_->height : height_;
  /* A pipeline bakes in the attachment formats; float colour targets, MRT
   * layouts and depth textures therefore need their own pipeline variants. */
  state_.color_target_count = 0;
  for (int slot = 0; slot < kWebGLMaxDrawBuffers; slot++) {
    if (draw_color_texture(slot) == nullptr) continue;
    state_.color_formats[state_.color_target_count++] = draw_color_format(slot);
  }
  if (state_.color_target_count == 0) state_.color_target_count = 1;
  state_.depth_format = target_depth_format();

  WebGLDrawKey key;
  key.program = current_program_;
  key.mode = draw_mode;
  key.index_size = index_size;
  key.element_buffer = !needs_expand && index_size > 0 ? bound_element_ : 0;
  key.attribs_version = attribs_version_;
  key.gpu_version = gpu_version_;
  key.framebuffer_version = framebuffer_version_;
  draw_state_hash(&key.state_hash, &key.state_hash2);

  const WebGLDrawSetup *setup = prepare_draw(key);
  if (setup == nullptr) return;

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

  /* Bindings belong to the render pass, so a setup reused across an upload (a
   * new pass) must be bound again even though nothing about it changed. */
  if (last_setup_ != setup || last_setup_pass_ != pass_serial_) {
    SDL_BindGPUGraphicsPipeline(pass_, setup->pipeline);
    if (!setup->bindings.empty()) {
      SDL_BindGPUVertexBuffers(pass_, 0, setup->bindings.data(),
                               (Uint32)setup->bindings.size());
    }
    SDL_GPUBuffer *index_buffer = index_size > 0 ? (needs_expand ? expanded_gpu : setup->index_buffer)
                                                 : nullptr;
    if (index_buffer != nullptr) {
      SDL_GPUBufferBinding binding;
      memset(&binding, 0, sizeof(binding));
      binding.buffer = index_buffer;
      binding.offset = 0;
      SDL_BindGPUIndexBuffer(pass_, &binding,
                             index_size == 4 ? SDL_GPU_INDEXELEMENTSIZE_32BIT
                                             : SDL_GPU_INDEXELEMENTSIZE_16BIT);
    }
    last_pipeline_ = setup->pipeline;
    last_setup_ = setup;
    last_setup_pass_ = pass_serial_;
  }
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
  /* User uniform blocks live in the same std140 block; refresh their members
   * from the CPU-side buffer bound to each block's binding point. */
  for (const WebGLUniformBlock &block : program_->blocks) {
    if (block.binding < 0 || block.binding >= 16) continue;
    const WebGLUniformBinding &binding = uniform_bindings_[block.binding];
    if (binding.buffer == 0) continue;
    WebGLBuffer *uniform_buffer = buffer(binding.buffer);
    if (uniform_buffer == nullptr) continue;
    size_t block_offset = binding.offset;
    size_t length = (size_t)block.data_size;
    if (binding.size > 0 && length > binding.size) length = binding.size;
    if (block_offset > uniform_buffer->data.size()) continue;
    if (length > uniform_buffer->data.size() - block_offset) {
      length = uniform_buffer->data.size() - block_offset;
    }
    if (block.base_offset >= 0 &&
        block.base_offset + (int)length <= program_->block_size) {
      memcpy(program_->block.data() + block.base_offset,
             uniform_buffer->data.data() + block_offset, length);
    }
  }

  if (program_->vs_has_block && program_->block_size > 0) {
    SDL_PushGPUVertexUniformData(cmd_, 0, program_->block.data(), (Uint32)program_->block.size());
  }
  if (program_->fs_has_block && program_->block_size > 0) {
    SDL_PushGPUFragmentUniformData(cmd_, 0, program_->block.data(),
                                   (Uint32)program_->block.size());
  }

  auto bind_samplers = [&](const std::vector<int> &stage, bool vertex_stage) {
    if (stage.empty()) return;
    sampler_bindings_.clear();
    sampler_bindings_.reserve(stage.size());
    for (int uniform_index : stage) {
      const WebGLUniform &uniform = program_->uniforms[(size_t)uniform_index];
      const int unit =
          uniform.unit >= 0 && uniform.unit < kWebGLMaxTextureUnits ? uniform.unit : 0;
      uint32_t texture_id = bound_textures_[unit];
      if (uniform.layout.gl_type == GL_SAMPLER_CUBE ||
          uniform.layout.gl_type == GL_SAMPLER_CUBE_SHADOW) {
        texture_id = bound_cube_textures_[unit];
      } else if (uniform.layout.gl_type == GL_SAMPLER_3D) {
        texture_id = bound_3d_textures_[unit];
      } else if (uniform.layout.gl_type == GL_SAMPLER_2D_ARRAY ||
                 uniform.layout.gl_type == GL_SAMPLER_2D_ARRAY_SHADOW) {
        texture_id = bound_array_textures_[unit];
      }
      WebGLTexture *sampled = texture(texture_id);
      SDL_GPUTexture *gpu = sampled != nullptr ? ensure_texture_gpu(sampled, sampled->as_target)
                                               : nullptr;
      WebGLSampler *sampler_object = this->sampler(bound_samplers_[unit]);
      SDL_GPUSampler *sampler = nullptr;
      if (sampler_object != nullptr) {
        sampler = sampler_gpu(sampler_object);
      } else if (sampled != nullptr) {
        sampler = uniform.compare ? sampler_for_compare(sampled) : sampler_for(sampled);
      }
      if (gpu == nullptr || sampler == nullptr) {
        gpu = fallback_texture_;
        sampler = fallback_sampler_;
      }
      SDL_GPUTextureSamplerBinding texture_binding;
      texture_binding.texture = gpu;
      texture_binding.sampler = sampler;
      sampler_bindings_.push_back(texture_binding);
    }
    if (vertex_stage) {
      SDL_BindGPUVertexSamplers(pass_, 0, sampler_bindings_.data(),
                                (Uint32)sampler_bindings_.size());
    } else {
      SDL_BindGPUFragmentSamplers(pass_, 0, sampler_bindings_.data(),
                                  (Uint32)sampler_bindings_.size());
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
