/*
 * xbintsc GUI engine — GLSL user uniform-block scanner.
 *
 * The block's members are merged into the engine's single std140 block under
 * `xt_ub_<instance>_<member>` names; `instance.member` references are renamed
 * by the rewriter. SDL_GPU has no bindable uniform buffers, so the draw path
 * copies the bound buffer into these members instead.
 */

#include "webgl_glsl.h"

#include <string>
#include <vector>

namespace xtgui {

bool scan_uniform_block(const std::vector<GlslToken> &tokens, size_t uniform_index,
                        size_t pending_layout, bool conditional, GlslShaderInfo *info,
                        size_t *after, size_t *first_uniform_any) {
  size_t brace = uniform_index + 1;
  while (brace < tokens.size() && tokens[brace].text != "{" && tokens[brace].text != ";") {
    brace++;
  }
  if (brace >= tokens.size() || tokens[brace].text != "{") return false;

  std::string block_name;
  for (size_t t = uniform_index + 1; t < brace; t++) {
    if (tokens[t].kind == GlslToken::Ident) block_name = tokens[t].text;
  }
  size_t close = brace;
  int braces = 0;
  for (; close < tokens.size(); close++) {
    if (tokens[close].text == "{") braces++;
    if (tokens[close].text == "}") {
      braces--;
      if (braces == 0) break;
    }
    if (tokens[close].kind == GlslToken::Directive) {
      info->unsupported = "preprocessor directives inside uniform blocks are not supported";
      *after = close;
      return true;
    }
  }
  if (close + 2 >= tokens.size() || tokens[close + 1].kind != GlslToken::Ident ||
      tokens[close + 2].text != ";") {
    info->unsupported = "uniform blocks without an instance name are not supported";
    *after = close;
    return true;
  }

  GlslUniformBlock block;
  block.name = block_name;
  block.instance = tokens[close + 1].text;
  block.begin = pending_layout != std::string::npos ? tokens[pending_layout].begin
                                                    : tokens[uniform_index].begin;
  block.end = tokens[close + 2].end;
  block.conditional = conditional;
  bool valid = !block.instance.empty();
  size_t member_start = brace + 1;
  int inner_braces = 0;
  for (size_t t = brace + 1; t < close && valid; t++) {
    if (tokens[t].text == "{") inner_braces++;
    if (tokens[t].text == "}") inner_braces--;
    if (!(tokens[t].text == ";" && inner_braces == 0)) continue;
    size_t at = member_start;
    while (at < t && tokens[at].kind == GlslToken::Ident && is_qualifier(tokens[at].text)) at++;
    if (at + 1 >= t || tokens[at].kind != GlslToken::Ident ||
        tokens[at + 1].kind != GlslToken::Ident) {
      valid = false;
      break;
    }
    GlslBlockMemberDecl member;
    member.type = tokens[at].text;
    member.name = tokens[at + 1].text;
    size_t array_at = at + 2;
    if (array_at < t && tokens[array_at].text == "[") {
      std::string array;
      for (size_t k = array_at; k < t; k++) array += tokens[k].text;
      member.array = array;
    }
    if (member.type.rfind("sampler", 0) == 0 || member.type == "atomic_uint" ||
        member.type == "struct") {
      valid = false;
      break;
    }
    block.members.push_back(member);
    member_start = t + 1;
  }
  if (!valid || block.members.empty()) {
    info->unsupported = "unsupported uniform block declaration";
    *after = close;
    return true;
  }

  for (size_t t = 0; t + 2 < tokens.size(); t++) {
    if (tokens[t].kind != GlslToken::Ident || tokens[t].text != block.instance) continue;
    if (tokens[t + 1].text != "." || tokens[t + 2].kind != GlslToken::Ident) continue;
    if (tokens[t].begin >= block.begin && tokens[t].begin < block.end) continue;
    bool is_member = false;
    for (const GlslBlockMemberDecl &member : block.members) {
      if (member.name == tokens[t + 2].text) is_member = true;
    }
    if (!is_member) continue;
    GlslRename rename;
    rename.begin = tokens[t].begin;
    rename.end = tokens[t + 2].end;
    rename.text = "xt_ub_" + block.instance + "_" + tokens[t + 2].text;
    info->renames.push_back(rename);
  }
  for (const GlslBlockMemberDecl &member : block.members) {
    GlslUniformDecl decl;
    decl.type = member.type;
    decl.name = "xt_ub_" + block.instance + "_" + member.name;
    decl.array = member.array;
    info->uniforms.push_back(decl);
  }
  if (*first_uniform_any == std::string::npos) *first_uniform_any = block.begin;
  if (!block.conditional && info->first_uniform_offset == std::string::npos) {
    info->first_uniform_offset = block.begin;
  }
  info->blocks.push_back(block);
  *after = close + 2;
  return true;
}

}  // namespace xtgui
