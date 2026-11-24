/*
 * xbintsc GUI engine — linked-program records.
 *
 * Split out of `webgl.h` to keep that header within the repository's file-length
 * budget. A program owns its reflected uniforms, the std140 block the page's
 * `uniform*` calls fill, and the pipeline cache keyed by draw state.
 */
#ifndef XT_GUI_WEBGL_PROGRAM_H
#define XT_GUI_WEBGL_PROGRAM_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "webgl_draw_state.h"
#include "webgl_glsl.h"
#include "webgl_uniform_block.h"

namespace xtgui {

/** One active uniform of a linked program, with its std140 home. */
struct WebGLUniform {
  std::string name;
  GlslBlockMember layout;
  bool sampler = false;
  /** `sampler2DShadow`/`samplerCubeShadow`/`sampler2DArrayShadow`: SDL_GPU
   * binds a comparison sampler, and the GLSL `texture()` call performs the
   * hardware depth compare. */
  bool compare = false;
  int unit = -1;
};

struct WebGLProgram {
  uint32_t id = 0;
  uint32_t vs = 0;
  uint32_t fs = 0;
  bool linked = false;
  std::string log;
  std::vector<WebGLUniform> uniforms;
  std::vector<WebGLUniformBlock> blocks;
  std::vector<GlslUniformDecl> attribs;
  std::vector<WebGLDrawAttribPlan> draw_attribs;
  std::unordered_map<std::string, int> attrib_locations;  // name -> location
  std::unordered_map<std::string, int> attrib_bindings;   // bindAttribLocation overrides
  std::unordered_map<std::string, int> uniform_index;     // name -> uniforms[] index
  std::vector<int> vs_sampler_uniforms;  // local binding -> uniforms[] index
  std::vector<int> fs_sampler_uniforms;
  std::vector<uint8_t> block;  // std140 uniform data
  int block_size = 0;
  bool block_dirty = true;
  bool vs_has_block = false;
  bool fs_has_block = false;
  SDL_GPUShader *vs_shader = nullptr;
  SDL_GPUShader *fs_shader = nullptr;
  std::vector<SDL_GPUGraphicsPipeline *> pipelines;
  std::unordered_map<WebGLDrawKey, SDL_GPUGraphicsPipeline *, WebGLDrawKeyHash> pipeline_cache;
};

}  // namespace xtgui

#endif /* XT_GUI_WEBGL_PROGRAM_H */
