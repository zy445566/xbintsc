/*
 * xbintsc GUI engine — helpers shared by the WebGL program and uniform units.
 */
#ifndef XT_GUI_WEBGL_PROGRAM_INTERNAL_H
#define XT_GUI_WEBGL_PROGRAM_INTERNAL_H

#include "webgl.h"
#include "webgl_glsl.h"

namespace xtgui {
namespace webgl_detail {

/** Bytes a std140 block member occupies (the whole array for an array). */
int member_bytes(const GlslBlockMember &member);

/** Fill `record->blocks` from the user blocks merged into the engine block. */
void build_uniform_blocks(WebGLProgram *record, const std::vector<GlslUniformBlock> &vertex_blocks,
                          const std::vector<GlslUniformBlock> &fragment_blocks,
                          const std::vector<GlslBlockMember> &members);

}  // namespace webgl_detail
}  // namespace xtgui

#endif /* XT_GUI_WEBGL_PROGRAM_INTERNAL_H */
