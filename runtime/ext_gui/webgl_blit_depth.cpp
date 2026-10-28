/*
 * xbintsc GUI engine — `blitFramebuffer` depth copies.
 *
 * SDL_GPU's blit copies colour attachments only. A depth attachment can still
 * be sampled as an ordinary texture, so a full-screen quad samples the source
 * depth and writes it back with `gl_FragDepth` while colour writes are masked
 * off. Stencil is not sampleable through SDL_GPU and is left untouched.
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

const char *kBlitVertexShader =
    "#version 300 es\n"
    "uniform highp vec4 u_dst;\n"
    "uniform highp vec4 u_uv;\n"
    "in highp vec2 a_pos;\n"
    "out highp vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = mix(u_uv.xy, u_uv.zw, a_pos);\n"
    "  gl_Position = vec4(mix(u_dst.xy, u_dst.zw, a_pos), 0.0, 1.0);\n"
    "}\n";

const char *kBlitFragmentShader =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform highp vec4 u_dst;\n"
    "uniform highp sampler2D u_depth;\n"
    "in highp vec2 v_uv;\n"
    "out highp vec4 fragColor;\n"
    "void main() {\n"
    "  fragColor = vec4(1.0);\n"
    "  gl_FragDepth = texture(u_depth, v_uv).r;\n"
    "}\n";

}  // namespace

bool WebGLContext::blit_depth(uint32_t source_framebuffer, uint32_t destination_framebuffer,
                              const int source[4], const int destination[4]) {
  if (guidev::g_device == nullptr) return false;
  if (source_framebuffer == destination_framebuffer) return false;
  /* A renderbuffer depth attachment is never created with SAMPLER usage. */
  if (source_framebuffer != 0) {
    WebGLFramebuffer *framebuffer = this->framebuffer(source_framebuffer);
    if (framebuffer == nullptr || framebuffer->depth_texture == 0) return false;
  }
  if (destination_framebuffer != 0 && this->framebuffer(destination_framebuffer) == nullptr) {
    return false;
  }

  int source_width = 0;
  int source_height = 0;
  int destination_width = 0;
  int destination_height = 0;
  framebuffer_size(source_framebuffer, &source_width, &source_height);
  framebuffer_size(destination_framebuffer, &destination_width, &destination_height);
  if (source_width <= 0 || source_height <= 0 || destination_width <= 0 ||
      destination_height <= 0) {
    return false;
  }
  int sx0 = source[0];
  int sy0 = source[1];
  int sx1 = source[2];
  int sy1 = source[3];
  int dx0 = destination[0];
  int dy0 = destination[1];
  int dx1 = destination[2];
  int dy1 = destination[3];
  if (sx1 < sx0) std::swap(sx0, sx1);
  if (sy1 < sy0) std::swap(sy0, sy1);
  if (dx1 < dx0) std::swap(dx0, dx1);
  if (dy1 < dy0) std::swap(dy0, dy1);
  sx0 = std::max(0, std::min(sx0, source_width));
  sx1 = std::max(0, std::min(sx1, source_width));
  sy0 = std::max(0, std::min(sy0, source_height));
  sy1 = std::max(0, std::min(sy1, source_height));
  dx0 = std::max(0, std::min(dx0, destination_width));
  dx1 = std::max(0, std::min(dx1, destination_width));
  dy0 = std::max(0, std::min(dy0, destination_height));
  dy1 = std::max(0, std::min(dy1, destination_height));
  if (sx1 <= sx0 || sy1 <= sy0 || dx1 <= dx0 || dy1 <= dy0) return true;
  SDL_GPUTexture *source_depth = framebuffer_depth(source_framebuffer);
  SDL_GPUTexture *destination_depth = framebuffer_depth(destination_framebuffer);
  if (source_depth == nullptr || destination_depth == nullptr ||
      source_depth == destination_depth) {
    return false;
  }

  /* Build the internal program/buffer on first use. */
  if (blit_program_ == 0) {
    const uint32_t vs = create_shader(GL_VERTEX_SHADER);
    shader_source(vs, kBlitVertexShader);
    const uint32_t fs = create_shader(GL_FRAGMENT_SHADER);
    shader_source(fs, kBlitFragmentShader);
    if (compile_shader(vs) && compile_shader(fs)) {
      const uint32_t program_id = create_program();
      attach_shader(program_id, vs);
      attach_shader(program_id, fs);
      bind_attrib_location(program_id, 0, "a_pos");
      if (link_program(program_id)) {
        blit_program_ = program_id;
      } else {
        delete_program(program_id);
      }
    }
    if (blit_program_ == 0) {
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
  WebGLProgram *program = this->program(blit_program_);
  WebGLBuffer *quad_buffer = buffer(blit_buffer_);
  SDL_GPUBuffer *quad_gpu = quad_buffer != nullptr ? ensure_buffer_gpu(quad_buffer) : nullptr;
  if (program == nullptr || program->vs_shader == nullptr || program->fs_shader == nullptr ||
      quad_gpu == nullptr || blit_sampler_ == nullptr) {
    return false;
  }

  /* NDC rectangle and source UVs. Source V is flipped because GL's origin is
   * bottom-left while SDL_GPU's render targets and uploads are top-down. */
  double dst[4] = {(double)dx0 / (double)destination_width * 2.0 - 1.0,
                   (double)dy0 / (double)destination_height * 2.0 - 1.0,
                   (double)dx1 / (double)destination_width * 2.0 - 1.0,
                   (double)dy1 / (double)destination_height * 2.0 - 1.0};
  double uv[4] = {(double)sx0 / (double)source_width,
                  1.0 - (double)sy0 / (double)source_height,
                  (double)sx1 / (double)source_width,
                  1.0 - (double)sy1 / (double)source_height};

  const WebGLDrawState saved_state = state_;
  const uint32_t saved_program = current_program_;
  state_.blend = false;
  state_.cull = false;
  state_.stencil = false;
  state_.depth_test = true;
  state_.depth_write = true;
  state_.depth_func = GL_ALWAYS;
  for (bool &channel : state_.color_mask) channel = false;
  state_.color_target_count = 0;
  for (int slot = 0; slot < kWebGLMaxDrawBuffers; slot++) {
    if (draw_color_texture(slot) == nullptr) continue;
    state_.color_formats[state_.color_target_count++] = draw_color_format(slot);
  }
  if (state_.color_target_count == 0) state_.color_target_count = 1;
  state_.depth_format = target_depth_format();
  current_program_ = blit_program_;

  std::string key = "blit-depth/";
  for (int i = 0; i < state_.color_target_count; i++) {
    key += std::to_string(state_.color_formats[i]) + ",";
  }
  key += std::to_string(state_.depth_format);
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
  auto cached = program->pipeline_cache.find(key);
  if (cached != program->pipeline_cache.end()) {
    pipeline = cached->second;
  } else {
    pipeline = create_pipeline(program, GL_TRIANGLE_STRIP, 0, 1, 1, &attribute, &buffer_description,
                               key);
  }
  bool ok = pipeline != nullptr;
  if (ok) {
    begin_pass(false, false, false);
    ok = pass_ != nullptr;
  }
  if (ok) {
    SDL_BindGPUGraphicsPipeline(pass_, pipeline);
    SDL_GPUBufferBinding binding;
    memset(&binding, 0, sizeof(binding));
    binding.buffer = quad_gpu;
    SDL_BindGPUVertexBuffers(pass_, 0, &binding, 1);
    SDL_Rect scissor;
    scissor.x = 0;
    scissor.y = 0;
    scissor.w = destination_width;
    scissor.h = destination_height;
    if (scissor_test_ && scissor_box_[2] > 0 && scissor_box_[3] > 0) {
      scissor.x = scissor_box_[0];
      scissor.y = destination_height - (scissor_box_[1] + scissor_box_[3]);
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
    if (scissor.x + scissor.w > destination_width) scissor.w = destination_width - scissor.x;
    if (scissor.y + scissor.h > destination_height) scissor.h = destination_height - scissor.y;
    if (scissor.w < 0) scissor.w = 0;
    if (scissor.h < 0) scissor.h = 0;
    SDL_SetGPUScissor(pass_, &scissor);

    const int dst_location = uniform_location(blit_program_, "u_dst");
    const int uv_location = uniform_location(blit_program_, "u_uv");
    if (dst_location >= 0) uniform_floats(blit_program_, dst_location, 4, dst, 1);
    if (uv_location >= 0) uniform_floats(blit_program_, uv_location, 4, uv, 1);
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
    sampler_binding.texture = source_depth;
    sampler_binding.sampler = blit_sampler_;
    SDL_BindGPUFragmentSamplers(pass_, 0, &sampler_binding, 1);
    SDL_DrawGPUPrimitives(pass_, 4, 1, 0, 0);
  }

  state_ = saved_state;
  current_program_ = saved_program;
  return ok;
}

}  // namespace xtgui
