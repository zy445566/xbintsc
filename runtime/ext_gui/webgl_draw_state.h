/*
 * xbintsc GUI engine — the fixed-function WebGL state a pipeline depends on.
 *
 * Split out of `webgl.h` to keep that header under the repository's
 * file-length budget.
 */
#ifndef XT_GUI_WEBGL_DRAW_STATE_H
#define XT_GUI_WEBGL_DRAW_STATE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "webgl_constants.h"

namespace xtgui {

/** One face's stencil test and update state. */
struct WebGLStencilFace {
  int func = GL_ALWAYS;
  int ref = 0;
  uint32_t value_mask = 0xFFFFFFFFu;  // compare mask
  uint32_t write_mask = 0xFFFFFFFFu;
  int fail = GL_KEEP;
  int zfail = GL_KEEP;
  int zpass = GL_KEEP;
};

/** The fixed-function state a pipeline depends on. */
struct WebGLDrawState {
  bool blend = false;
  int blend_src_rgb = GL_ONE;
  int blend_dst_rgb = GL_ZERO;
  int blend_src_alpha = GL_ONE;
  int blend_dst_alpha = GL_ZERO;
  int blend_eq_rgb = GL_FUNC_ADD;
  int blend_eq_alpha = GL_FUNC_ADD;
  bool depth_test = false;
  bool depth_write = true;
  int depth_func = GL_LESS;
  bool stencil = false;
  WebGLStencilFace stencil_front;
  WebGLStencilFace stencil_back;
  bool cull = false;
  int cull_face = GL_BACK;
  int front_face = GL_CCW;
  bool color_mask[4] = {true, true, true, true};
  /** Attachment formats of the bound target; pipelines are format-specific. */
  int color_formats[4] = {
      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};
  int color_target_count = 1;
  int depth_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
};

/**
 * Identity of one prepared draw. Everything a draw's pipeline, vertex layout
 * and GPU buffer handles depend on is either an exact integer here or folds
 * into `state_hash`/`state_hash2` (a 128-bit hash of `WebGLDrawState`). Two
 * independent hashes keep a collision in the same place a 128-bit hash would
 * have it — the state struct has no padding-sensitive fields to compare.
 */
struct WebGLDrawKey {
  uint32_t program = 0;
  int mode = 0;                // GL primitive mode
  int index_size = 0;          // 0 = drawArrays, otherwise index element bytes
  uint32_t element_buffer = 0; // bound GL_ELEMENT_ARRAY_BUFFER, 0 = none
  uint64_t attribs_version = 0;
  uint64_t gpu_version = 0;
  uint64_t framebuffer_version = 0;
  uint64_t state_hash = 0;
  uint64_t state_hash2 = 0;

  bool operator==(const WebGLDrawKey &other) const {
    return program == other.program && mode == other.mode && index_size == other.index_size &&
           element_buffer == other.element_buffer && attribs_version == other.attribs_version &&
           gpu_version == other.gpu_version &&
           framebuffer_version == other.framebuffer_version && state_hash == other.state_hash &&
           state_hash2 == other.state_hash2;
  }
};

struct WebGLDrawKeyHash {
  size_t operator()(const WebGLDrawKey &key) const {
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](uint64_t value) {
      hash ^= value;
      hash *= 1099511628211ull;
      hash ^= hash >> 29;
    };
    mix(key.program);
    mix((uint64_t)(uint32_t)key.mode);
    mix((uint64_t)(uint32_t)key.index_size);
    mix(key.element_buffer);
    mix(key.attribs_version);
    mix(key.gpu_version);
    mix(key.framebuffer_version);
    mix(key.state_hash);
    mix(key.state_hash2);
    return (size_t)hash;
  }
};

/**
 * One active vertex attribute, resolved once at link time: `location` is the
 * base attribute location and `type` the GLSL type name the draw needs to size
 * the vertex layout. Keeping this out of the draw path removes the per-draw
 * `attribs` scan and the `name -> location` string hash lookup.
 */
struct WebGLDrawAttribPlan {
  int location = -1;
  std::string type;
};

/** A pipeline and the vertex-layout/binding set the draw prepared for it. A
 * cache hit is a bind (when it changed) and a draw. */
struct WebGLDrawSetup {
  SDL_GPUGraphicsPipeline *pipeline = nullptr;
  std::vector<SDL_GPUVertexAttribute> attributes;
  std::vector<SDL_GPUVertexBufferDescription> buffers;
  std::vector<SDL_GPUBufferBinding> bindings;
  SDL_GPUBuffer *index_buffer = nullptr;
};

}  // namespace xtgui

#endif /* XT_GUI_WEBGL_DRAW_STATE_H */
