/*
 * xbintsc GUI engine — WebGL pipelines and GPU helper resources.
 *
 * SDL_GPU has no dynamic fixed-function state: blend, depth and rasterisation
 * live in an `SDL_GPUGraphicsPipeline`. WebGL changes them with `glEnable` and
 * friends, so a pipeline is built lazily per (program, vertex layout, state) and
 * cached on the program. This unit also owns the fallback texture used for an
 * unbound sampler and the transient buffers a draw needs.
 */

#include "webgl.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gui_internal.h"

namespace xtgui {
namespace {

SDL_GPUBlendFactor blend_factor(int factor) {
  switch (factor) {
    case GL_ZERO:
      return SDL_GPU_BLENDFACTOR_ZERO;
    case GL_ONE:
      return SDL_GPU_BLENDFACTOR_ONE;
    case GL_SRC_COLOR:
      return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case GL_ONE_MINUS_SRC_COLOR:
      return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case GL_DST_COLOR:
      return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case GL_ONE_MINUS_DST_COLOR:
      return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    case GL_SRC_ALPHA:
      return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA:
      return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA:
      return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA:
      return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case GL_CONSTANT_COLOR:
    case GL_CONSTANT_ALPHA:
      return SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
    case GL_ONE_MINUS_CONSTANT_COLOR:
    case GL_ONE_MINUS_CONSTANT_ALPHA:
      return SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
    case GL_SRC_ALPHA_SATURATE:
      return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
    default:
      return SDL_GPU_BLENDFACTOR_ONE;
  }
}

SDL_GPUBlendOp blend_op(int op) {
  switch (op) {
    case GL_FUNC_SUBTRACT:
      return SDL_GPU_BLENDOP_SUBTRACT;
    case GL_FUNC_REVERSE_SUBTRACT:
      return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
    default:
      return SDL_GPU_BLENDOP_ADD;
  }
}

SDL_GPUCompareOp compare_op(int func) {
  switch (func) {
    case GL_NEVER:
      return SDL_GPU_COMPAREOP_NEVER;
    case GL_LESS:
      return SDL_GPU_COMPAREOP_LESS;
    case GL_EQUAL:
      return SDL_GPU_COMPAREOP_EQUAL;
    case GL_LEQUAL:
      return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    case GL_GREATER:
      return SDL_GPU_COMPAREOP_GREATER;
    case GL_NOTEQUAL:
      return SDL_GPU_COMPAREOP_NOT_EQUAL;
    case GL_GEQUAL:
      return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
    default:
      return SDL_GPU_COMPAREOP_ALWAYS;
  }
}

SDL_GPUStencilOp stencil_op(int op) {
  switch (op) {
    case GL_ZERO:
      return SDL_GPU_STENCILOP_ZERO;
    case GL_REPLACE:
      return SDL_GPU_STENCILOP_REPLACE;
    case GL_INCR:
      return SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP;
    case GL_INCR_WRAP:
      return SDL_GPU_STENCILOP_INCREMENT_AND_WRAP;
    case GL_DECR:
      return SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP;
    case GL_DECR_WRAP:
      return SDL_GPU_STENCILOP_DECREMENT_AND_WRAP;
    case GL_INVERT:
      return SDL_GPU_STENCILOP_INVERT;
    default:
      return SDL_GPU_STENCILOP_KEEP;
  }
}

/** SDL 3.2 has no per-pipeline stencil write mask. A mask of 0 means "write
 * nothing", which every op can express as KEEP; a partial mask cannot be
 * represented, so the op applies to all bits. */
SDL_GPUStencilOpState stencil_face_state(const WebGLStencilFace &face) {
  SDL_GPUStencilOpState state;
  memset(&state, 0, sizeof(state));
  if (face.write_mask == 0) {
    state.fail_op = SDL_GPU_STENCILOP_KEEP;
    state.pass_op = SDL_GPU_STENCILOP_KEEP;
    state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
  } else {
    state.fail_op = stencil_op(face.fail);
    state.pass_op = stencil_op(face.zpass);
    state.depth_fail_op = stencil_op(face.zfail);
  }
  state.compare_op = compare_op(face.func);
  return state;
}

SDL_GPUPrimitiveType primitive_of(int mode) {
  switch (mode) {
    case GL_POINTS:
      return SDL_GPU_PRIMITIVETYPE_POINTLIST;
    case GL_LINES:
      return SDL_GPU_PRIMITIVETYPE_LINELIST;
    case GL_LINE_STRIP:
      return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
    case GL_TRIANGLES:
      return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    case GL_TRIANGLE_STRIP:
      return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    default:
      return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  }
}

}  // namespace

bool WebGLContext::ensure_fallback() {
  if (fallback_texture_ != nullptr && fallback_sampler_ != nullptr) return true;
  if (guidev::g_device == nullptr) return false;
  if (fallback_texture_ == nullptr) {
    SDL_GPUTextureCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = 1;
    info.height = 1;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    fallback_texture_ = SDL_CreateGPUTexture(guidev::g_device, &info);
    if (fallback_texture_ != nullptr) {
      end_pass();
      ensure_command();
      if (cmd_ != nullptr) {
        SDL_GPUTransferBufferCreateInfo transfer_info;
        memset(&transfer_info, 0, sizeof(transfer_info));
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = 4;
        SDL_GPUTransferBuffer *transfer =
            SDL_CreateGPUTransferBuffer(guidev::g_device, &transfer_info);
        if (transfer != nullptr) {
          void *mapped = SDL_MapGPUTransferBuffer(guidev::g_device, transfer, false);
          static const uint8_t white[4] = {255, 255, 255, 255};
          memcpy(mapped, white, 4);
          SDL_UnmapGPUTransferBuffer(guidev::g_device, transfer);
          SDL_GPUTextureTransferInfo source;
          memset(&source, 0, sizeof(source));
          source.transfer_buffer = transfer;
          source.pixels_per_row = 1;
          source.rows_per_layer = 1;
          SDL_GPUTextureRegion region;
          memset(&region, 0, sizeof(region));
          region.texture = fallback_texture_;
          region.w = 1;
          region.h = 1;
          region.d = 1;
          SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd_);
          SDL_UploadToGPUTexture(copy, &source, &region, false);
          SDL_EndGPUCopyPass(copy);
          SDL_ReleaseGPUTransferBuffer(guidev::g_device, transfer);
        }
      }
    }
  }
  if (fallback_sampler_ == nullptr) {
    SDL_GPUSamplerCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.max_lod = 0.0f;
    fallback_sampler_ = SDL_CreateGPUSampler(guidev::g_device, &info);
  }
  return fallback_texture_ != nullptr && fallback_sampler_ != nullptr;
}

SDL_GPUBuffer *WebGLContext::upload_temp_buffer(const void *data, uint32_t size,
                                                SDL_GPUBufferUsageFlags usage) {
  if (guidev::g_device == nullptr || size == 0) return nullptr;
  end_pass();
  ensure_command();
  if (cmd_ == nullptr) return nullptr;
  SDL_GPUBufferCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.usage = usage;
  info.size = size;
  SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer(guidev::g_device, &info);
  if (buffer == nullptr) return nullptr;
  SDL_GPUTransferBufferCreateInfo transfer_info;
  memset(&transfer_info, 0, sizeof(transfer_info));
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_info.size = size;
  SDL_GPUTransferBuffer *transfer =
      SDL_CreateGPUTransferBuffer(guidev::g_device, &transfer_info);
  if (transfer == nullptr) {
    SDL_ReleaseGPUBuffer(guidev::g_device, buffer);
    return nullptr;
  }
  void *mapped = SDL_MapGPUTransferBuffer(guidev::g_device, transfer, false);
  memcpy(mapped, data, size);
  SDL_UnmapGPUTransferBuffer(guidev::g_device, transfer);
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd_);
  SDL_GPUTransferBufferLocation source;
  memset(&source, 0, sizeof(source));
  source.transfer_buffer = transfer;
  SDL_GPUBufferRegion region;
  memset(&region, 0, sizeof(region));
  region.buffer = buffer;
  region.size = size;
  SDL_UploadToGPUBuffer(copy, &source, &region, false);
  SDL_EndGPUCopyPass(copy);
  SDL_ReleaseGPUTransferBuffer(guidev::g_device, transfer);
  temp_buffers_.push_back(buffer);
  return buffer;
}

SDL_GPUGraphicsPipeline *WebGLContext::create_pipeline(
    WebGLProgram *program, int mode, int index_size, int attribute_count, int buffer_count,
    const SDL_GPUVertexAttribute *attrs, const SDL_GPUVertexBufferDescription *buffers,
    const std::string &key) {
  (void)index_size;
  SDL_GPUColorTargetDescription color_target;
  memset(&color_target, 0, sizeof(color_target));
  color_target.format = (SDL_GPUTextureFormat)state_.color_format;
  color_target.blend_state.enable_blend = state_.blend;
  color_target.blend_state.src_color_blendfactor = blend_factor(state_.blend_src_rgb);
  color_target.blend_state.dst_color_blendfactor = blend_factor(state_.blend_dst_rgb);
  color_target.blend_state.color_blend_op = blend_op(state_.blend_eq_rgb);
  color_target.blend_state.src_alpha_blendfactor = blend_factor(state_.blend_src_alpha);
  color_target.blend_state.dst_alpha_blendfactor = blend_factor(state_.blend_dst_alpha);
  color_target.blend_state.alpha_blend_op = blend_op(state_.blend_eq_alpha);
  color_target.blend_state.enable_color_write_mask = true;
  color_target.blend_state.color_write_mask =
      (state_.color_mask[0] ? SDL_GPU_COLORCOMPONENT_R : 0) |
      (state_.color_mask[1] ? SDL_GPU_COLORCOMPONENT_G : 0) |
      (state_.color_mask[2] ? SDL_GPU_COLORCOMPONENT_B : 0) |
      (state_.color_mask[3] ? SDL_GPU_COLORCOMPONENT_A : 0);

  SDL_GPUGraphicsPipelineCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.vertex_shader = program->vs_shader;
  info.fragment_shader = program->fs_shader;
  info.vertex_input_state.vertex_buffer_descriptions = buffers;
  info.vertex_input_state.num_vertex_buffers = (Uint32)buffer_count;
  info.vertex_input_state.vertex_attributes = attrs;
  info.vertex_input_state.num_vertex_attributes = (Uint32)attribute_count;
  info.primitive_type = primitive_of(mode);
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode =
      state_.cull ? (state_.cull_face == GL_FRONT
                         ? SDL_GPU_CULLMODE_FRONT
                         : (state_.cull_face == GL_FRONT_AND_BACK ? SDL_GPU_CULLMODE_BACK
                                                                  : SDL_GPU_CULLMODE_BACK))
                  : SDL_GPU_CULLMODE_NONE;
  info.rasterizer_state.front_face =
      state_.front_face == GL_CW ? SDL_GPU_FRONTFACE_CLOCKWISE
                                 : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  info.depth_stencil_state.enable_depth_test = state_.depth_test;
  info.depth_stencil_state.enable_depth_write = state_.depth_write;
  info.depth_stencil_state.compare_op = compare_op(state_.depth_func);
  info.depth_stencil_state.enable_stencil_test = state_.stencil;
  info.depth_stencil_state.front_stencil_state = stencil_face_state(state_.stencil_front);
  info.depth_stencil_state.back_stencil_state = stencil_face_state(state_.stencil_back);
  /* SDL_GPU has one compare/write mask for both faces; use the front-face
   * values (WebGL's default sets both faces alike). */
  info.depth_stencil_state.compare_mask = (Uint8)(state_.stencil_front.value_mask & 0xFF);
  info.depth_stencil_state.write_mask = (Uint8)(state_.stencil_front.write_mask & 0xFF);
  info.target_info.color_target_descriptions = &color_target;
  info.target_info.num_color_targets = 1;
  info.target_info.depth_stencil_format = (SDL_GPUTextureFormat)state_.depth_format;
  info.target_info.has_depth_stencil_target = true;

  SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(guidev::g_device, &info);
  if (pipeline == nullptr) {
    fprintf(stderr, "xt_gui: WebGL pipeline creation failed: %s\n", SDL_GetError());
    set_error(GL_INVALID_OPERATION);
    return nullptr;
  }
  program->pipeline_cache[key] = pipeline;
  program->pipelines.push_back(pipeline);
  return pipeline;
}

}  // namespace xtgui