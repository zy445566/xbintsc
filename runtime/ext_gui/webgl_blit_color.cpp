/*
 * xbintsc GUI engine — colour quad into a multisampled target.
 *
 * `SDL_BlitGPUTexture` cannot target a multisampled texture (its helper
 * pipeline is single-sampled), so a scissored colour clear on an antialiased
 * drawing buffer is a textured quad drawn into the current pass instead. The
 * quad covers exactly the scissor rectangle and its edges fall on pixel
 * boundaries, so the MSAA rasteriser gives every covered pixel full coverage.
 *
 * The internal program, quad buffer and sampler are ordinary engine objects and
 * live for the context's lifetime. The caller's GL state is saved/restored
 * around the draw.
 */

#include "webgl.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "gui_internal.h"

namespace xtgui {
namespace {

const char *kColorBlitVertexShader =
    "#version 300 es\n"
    "uniform highp vec4 u_dst;\n"
    "uniform highp vec4 u_uv;\n"
    "in highp vec2 a_pos;\n"
    "out highp vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = mix(u_uv.xy, u_uv.zw, a_pos);\n"
    "  gl_Position = vec4(mix(u_dst.xy, u_dst.zw, a_pos), 0.0, 1.0);\n"
    "}\n";

const char *kColorBlitFragmentShader =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform highp sampler2D u_color;\n"
    "in highp vec2 v_uv;\n"
    "out highp vec4 fragColor;\n"
    "void main() { fragColor = texture(u_color, v_uv); }\n";

}  // namespace

bool WebGLContext::draw_color_quad(SDL_GPUTexture *source, int source_width, int source_height,
                                   const int destination[4]) {
  if (guidev::g_device == nullptr || source == nullptr) return false;
  if (source_width <= 0 || source_height <= 0) return false;
  int target_width = 0;
  int target_height = 0;
  framebuffer_size(bound_framebuffer_, &target_width, &target_height);
  if (target_width <= 0 || target_height <= 0) return false;

  if (blit_color_program_ == 0) {
    const uint32_t vs = create_shader(GL_VERTEX_SHADER);
    shader_source(vs, kColorBlitVertexShader);
    const uint32_t fs = create_shader(GL_FRAGMENT_SHADER);
    shader_source(fs, kColorBlitFragmentShader);
    if (compile_shader(vs) && compile_shader(fs)) {
      const uint32_t program_id = create_program();
      attach_shader(program_id, vs);
      attach_shader(program_id, fs);
      bind_attrib_location(program_id, 0, "a_pos");
      if (link_program(program_id)) {
        blit_color_program_ = program_id;
      } else {
        delete_program(program_id);
      }
    }
    if (blit_color_program_ == 0) {
      delete_shader(vs);
      delete_shader(fs);
      return false;
    }
  }
  if (blit_buffer_ == 0) {
    blit_buffer_ = create_buffer();
    bind_buffer(GL_ARRAY_BUFFER, blit_buffer_);
    const float vertices[8] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    buffer_data(blit_buffer_, (const uint8_t *)vertices, (uint32_t)sizeof(vertices),
                GL_STATIC_DRAW);
  }
  if (blit_sampler_ == nullptr) {
    SDL_GPUSamplerCreateInfo sampler_info;
    memset(&sampler_info, 0, sizeof(sampler_info));
    sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.max_lod = 0.0f;
    blit_sampler_ = SDL_CreateGPUSampler(guidev::g_device, &sampler_info);
  }
  WebGLProgram *program = this->program(blit_color_program_);
  WebGLBuffer *quad_buffer = buffer(blit_buffer_);
  SDL_GPUBuffer *quad_gpu = quad_buffer != nullptr ? ensure_buffer_gpu(quad_buffer) : nullptr;
  if (program == nullptr || program->vs_shader == nullptr || program->fs_shader == nullptr ||
      quad_gpu == nullptr || blit_sampler_ == nullptr) {
    return false;
  }

  int x0 = destination[0];
  int y0 = destination[1];
  int x1 = destination[2];
  int y1 = destination[3];
  if (x1 < x0) std::swap(x0, x1);
  if (y1 < y0) std::swap(y0, y1);
  x0 = std::max(0, std::min(x0, target_width));
  x1 = std::max(0, std::min(x1, target_width));
  y0 = std::max(0, std::min(y0, target_height));
  y1 = std::max(0, std::min(y1, target_height));
  if (x1 <= x0 || y1 <= y0) return true;

  /* NDC rectangle and source UVs. Source V is flipped because GL's origin is
   * bottom-left while SDL_GPU render targets and uploads are top-down. */
  double dst[4] = {(double)x0 / (double)target_width * 2.0 - 1.0,
                   (double)y0 / (double)target_height * 2.0 - 1.0,
                   (double)x1 / (double)target_width * 2.0 - 1.0,
                   (double)y1 / (double)target_height * 2.0 - 1.0};
  double uv[4] = {0.0, 1.0, 1.0, 0.0};

  const WebGLDrawState saved_state = state_;
  const uint32_t saved_program = current_program_;
  state_.blend = false;
  state_.cull = false;
  state_.stencil = false;
  state_.depth_test = false;
  state_.depth_write = false;
  state_.depth_func = GL_ALWAYS;
  for (bool &channel : state_.color_mask) channel = true;
  state_.color_target_count = 0;
  for (int slot = 0; slot < kWebGLMaxDrawBuffers; slot++) {
    if (draw_color_texture(slot) == nullptr) continue;
    state_.color_formats[state_.color_target_count++] = draw_color_format(slot);
  }
  if (state_.color_target_count == 0) state_.color_target_count = 1;
  state_.depth_format = target_depth_format();
  state_.sample_count = target_samples();
  current_program_ = blit_color_program_;

  std::string key = "blit-color/";
  for (int i = 0; i < state_.color_target_count; i++) {
    key += std::to_string(state_.color_formats[i]) + ",";
  }
  key += std::to_string(state_.depth_format) + "/" + std::to_string(state_.sample_count);
  SDL_GPUVertexAttribute attribute;
  memset(&attribute, 0, sizeof(attribute));
  attribute.location = 0;
  attribute.buffer_slot = 0;
  attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
  attribute.offset = 0;
  SDL_GPUVertexBufferDescription buffer_description;
  memset(&buffer_description, 0, sizeof(buffer_description));
  buffer_description.slot = 0;
  buffer_description.pitch = 8;
  buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  SDL_GPUGraphicsPipeline *pipeline = nullptr;
  auto cached = internal_pipelines_.find(key);
  if (cached != internal_pipelines_.end()) {
    pipeline = cached->second;
  } else {
    pipeline = create_pipeline(program, GL_TRIANGLE_STRIP, 1, 1, &attribute,
                               &buffer_description);
    if (pipeline != nullptr) internal_pipelines_[key] = pipeline;
  }
  bool ok = pipeline != nullptr;
  if (ok) {
    begin_pass(false, false, false);
    ok = pass_ != nullptr;
  }
  if (ok) {
    SDL_GPUViewport viewport;
    memset(&viewport, 0, sizeof(viewport));
    viewport.x = 0;
    viewport.y = 0;
    viewport.w = (float)target_width;
    viewport.h = (float)target_height;
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(pass_, &viewport);
    SDL_Rect scissor;
    scissor.x = x0;
    scissor.y = target_height - y1;
    scissor.w = x1 - x0;
    scissor.h = y1 - y0;
    SDL_SetGPUScissor(pass_, &scissor);

    SDL_BindGPUGraphicsPipeline(pass_, pipeline);
    SDL_GPUBufferBinding binding;
    memset(&binding, 0, sizeof(binding));
    binding.buffer = quad_gpu;
    SDL_BindGPUVertexBuffers(pass_, 0, &binding, 1);
    const int dst_location = uniform_location(blit_color_program_, "u_dst");
    const int uv_location = uniform_location(blit_color_program_, "u_uv");
    if (dst_location >= 0) uniform_floats(blit_color_program_, dst_location, 4, dst, 1);
    if (uv_location >= 0) uniform_floats(blit_color_program_, uv_location, 4, uv, 1);
    if (program->block_size > 0) {
      if (program->vs_has_block) {
        SDL_PushGPUVertexUniformData(cmd_, 0, program->block.data(),
                                     (Uint32)program->block.size());
      }
      if (program->fs_has_block) {
        SDL_PushGPUFragmentUniformData(cmd_, 0, program->block.data(),
                                       (Uint32)program->block.size());
      }
    }
    SDL_GPUTextureSamplerBinding sampler_binding;
    sampler_binding.texture = source;
    sampler_binding.sampler = blit_sampler_;
    SDL_BindGPUFragmentSamplers(pass_, 0, &sampler_binding, 1);
    SDL_DrawGPUPrimitives(pass_, 4, 1, 0, 0);
  }

  state_ = saved_state;
  current_program_ = saved_program;
  /* This pass bound the blit pipeline directly, outside the prepared-draw
   * cache; a later draw in the same pass must bind its own pipeline again. */
  last_setup_ = nullptr;
  last_pipeline_ = nullptr;
  return ok;
}

}  // namespace xtgui
