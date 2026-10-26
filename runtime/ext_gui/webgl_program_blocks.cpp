/*
 * xbintsc GUI engine — user uniform blocks reflected from the merged block.
 */

#include "webgl.h"

#include <algorithm>
#include <vector>

#include "webgl_program_internal.h"

namespace xtgui {
namespace webgl_detail {

void build_uniform_blocks(WebGLProgram *record, const std::vector<GlslUniformBlock> &vertex_blocks,
                          const std::vector<GlslUniformBlock> &fragment_blocks,
                          const std::vector<GlslBlockMember> &members) {
  if (record == nullptr) return;
  /* Build the user-facing uniform-block table. Members were merged into the
   * engine block as `__ub_<instance>_<member>`, so their reflected offsets give
   * the block's base and size without a second descriptor set. */
  auto add_blocks = [&](const std::vector<GlslUniformBlock> &blocks) {
    for (const GlslUniformBlock &decl : blocks) {
      bool exists = false;
      for (const WebGLUniformBlock &existing : record->blocks) {
        if (existing.instance == decl.instance) exists = true;
      }
      if (exists) continue;
      WebGLUniformBlock block;
      block.name = decl.name;
      block.instance = decl.instance;
      int base = -1;
      int end = 0;
      int count = 0;
      for (const GlslBlockMemberDecl &member : decl.members) {
        const std::string name = "xt_ub_" + decl.instance + "_" + member.name;
        for (const GlslBlockMember &reflected : members) {
          if (reflected.name != name && reflected.name != name + "[0]") continue;
          if (base < 0 || reflected.offset < base) base = reflected.offset;
          end = std::max(end, reflected.offset + webgl_detail::member_bytes(reflected));
          count++;
          break;
        }
      }
      if (base < 0 || count == 0) continue;
      block.base_offset = base;
      block.data_size = ((end - base) + 15) & ~15;
      block.member_count = count;
      record->blocks.push_back(block);
    }
  };
  add_blocks(vertex_blocks);
  add_blocks(fragment_blocks);

}

}  // namespace webgl_detail
}  // namespace xtgui
