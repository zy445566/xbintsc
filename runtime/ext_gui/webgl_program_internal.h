/*
 * xbintsc GUI engine — helpers shared by the WebGL program and uniform units.
 */
#ifndef XT_GUI_WEBGL_PROGRAM_INTERNAL_H
#define XT_GUI_WEBGL_PROGRAM_INTERNAL_H

#include "webgl.h"
#include "webgl_glsl.h"

namespace xtgui {
namespace webgl_detail {

/** The GL enum reported for a GLSL uniform type (`getActiveUniform`). */
inline int gl_type_of(const std::string &type) {
  if (type == "float") return GL_FLOAT;
  if (type == "vec2") return GL_FLOAT_VEC2;
  if (type == "vec3") return GL_FLOAT_VEC3;
  if (type == "vec4") return GL_FLOAT_VEC4;
  if (type == "int" || type == "uint") return GL_INT;
  if (type == "ivec2" || type == "uvec2") return GL_INT_VEC2;
  if (type == "ivec3" || type == "uvec3") return GL_INT_VEC3;
  if (type == "ivec4" || type == "uvec4") return GL_INT_VEC4;
  if (type == "bool") return GL_BOOL;
  if (type == "bvec2") return GL_BOOL_VEC2;
  if (type == "bvec3") return GL_BOOL_VEC3;
  if (type == "bvec4") return GL_BOOL_VEC4;
  if (type == "mat2") return GL_FLOAT_MAT2;
  if (type == "mat3") return GL_FLOAT_MAT3;
  if (type == "mat4") return GL_FLOAT_MAT4;
  if (type == "sampler2D") return GL_SAMPLER_2D;
  if (type == "samplerCube") return GL_SAMPLER_CUBE;
  if (type == "sampler3D") return GL_SAMPLER_3D;
  if (type == "sampler2DArray") return GL_SAMPLER_2D_ARRAY;
  if (type == "sampler2DShadow") return GL_SAMPLER_2D_SHADOW;
  if (type == "samplerCubeShadow") return GL_SAMPLER_CUBE_SHADOW;
  if (type == "sampler2DArrayShadow") return GL_SAMPLER_2D_ARRAY_SHADOW;
  return 0;
}

/** How many consecutive attribute/varying locations a type consumes. */
inline int location_slots(const std::string &type) {
  if (type == "mat2") return 2;
  if (type == "mat3") return 3;
  if (type == "mat4") return 4;
  return 1;
}

/** Bytes a std140 block member occupies (the whole array for an array). */
int member_bytes(const GlslBlockMember &member);

/** Fill `record->blocks` from the user blocks merged into the engine block. */
void build_uniform_blocks(WebGLProgram *record, const std::vector<GlslUniformBlock> &vertex_blocks,
                          const std::vector<GlslUniformBlock> &fragment_blocks,
                          const std::vector<GlslBlockMember> &members);

}  // namespace webgl_detail
}  // namespace xtgui

#endif /* XT_GUI_WEBGL_PROGRAM_INTERNAL_H */
