/*
 * xbintsc GUI engine — WebGL GLSL rewriter.
 *
 * Turns the GLSL ES 1.00/3.00 a WebGL program supplies into the Vulkan-dialect
 * GLSL ES 3.10 glslang accepts: explicit attribute/varying/output locations, one
 * std140 uniform block (set 1 for vertex, set 3 for fragment), samplers with an
 * explicit descriptor set/binding, and the legacy `texture2D` / `gl_FragColor`
 * spellings. The original text is spliced, so comments, spacing and `#if`
 * blocks survive.
 */

#include "webgl_glsl.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace xtgui {
namespace {

struct Edit {
  size_t begin = 0;
  size_t end = 0;
  std::string text;
  int order = 0;
};

std::string trim(const std::string &text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace((unsigned char)text[begin]) != 0) begin++;
  while (end > begin && std::isspace((unsigned char)text[end - 1]) != 0) end--;
  return text.substr(begin, end - begin);
}

/** `<type> <name><array>` — the shape a block member or a layout-qualified
 * declaration needs. */
std::string declarator_text(const GlslUniformDecl &decl) {
  return decl.type + " " + decl.name + decl.array;
}

/** The same, keeping a precision qualifier (allowed on `in`/`out`/sampler
 * declarations but not needed on block members). */
std::string qualified_declarator_text(const GlslUniformDecl &decl) {
  std::string text;
  if (!decl.precision.empty()) text = decl.precision + " ";
  return text + declarator_text(decl);
}

bool has_float_precision(const std::string &source) {
  size_t at = 0;
  while ((at = source.find("precision", at)) != std::string::npos) {
    const size_t end = source.find(';', at);
    if (end == std::string::npos) break;
    const std::string line = source.substr(at, end - at);
    if (line.find("float") != std::string::npos) return true;
    at = end;
  }
  return false;
}

/** Replace the `#version` line (or prepend one) and append the preamble the
 * rewritten shader needs. */
std::string version_block(const std::string &source, bool fragment, bool needs_output,
                          bool *found_version, size_t *version_begin, size_t *version_end) {
  *found_version = false;
  *version_begin = 0;
  *version_end = 0;
  size_t line = 0;
  while (line < source.size()) {
    size_t line_end = source.find('\n', line);
    if (line_end == std::string::npos) line_end = source.size();
    const std::string text = trim(source.substr(line, line_end - line));
    if (!text.empty() && text[0] != '#') break;
    if (text.rfind("#version", 0) == 0) {
      *found_version = true;
      *version_begin = line;
      *version_end = line_end;
      break;
    }
    if (!text.empty() && text[0] == '#' && text.rfind("#extension", 0) != 0 &&
        text.rfind("#version", 0) != 0) {
      /* A non-version directive before `#version` is illegal, but keep looking
       * so the rewritten shader still gets a version line. */
    }
    line = line_end + 1;
  }
  std::string block = "#version 310 es\n";
  if (fragment) {
    if (!has_float_precision(source)) block += "precision highp float;\nprecision highp int;\n";
    if (needs_output) block += "layout(location = 0) out highp vec4 xt_FragColor;\n";
  }
  return block;
}

}  // namespace

std::string rewrite_glsl(const std::string &source, bool fragment, const GlslLinkLayout &layout,
                         const GlslShaderInfo &info) {
  std::vector<Edit> edits;
  bool found_version = false;
  size_t version_begin = 0;
  size_t version_end = 0;
  /* A GLSL ES 3.00 source may already declare its own output (three.js emits
   * `layout(location = 0) out highp vec4 pc_fragColor;` and defines
   * `gl_FragColor` to it). In that case the legacy name maps to the declared
   * output instead of adding a second declaration at location 0. */
  std::string fragment_output_name = "xt_FragColor";
  if (fragment && info.has_fragment_output) {
    for (const GlslDeclSite &site : info.sites) {
      if (site.kind == GlslDeclSite::FragmentOut) {
        fragment_output_name = site.decl.name;
        break;
      }
    }
  }
  const bool needs_output =
      fragment && info.legacy_fragment_output && !info.has_fragment_output;
  const std::string block =
      version_block(source, fragment, needs_output, &found_version, &version_begin, &version_end);

  Edit version;
  version.begin = version_begin;
  version.end = version_end;
  version.text = block;
  version.order = 0;
  edits.push_back(version);

  /* Remove extension directives that are core in ES 3.10 (or unsupported). */
  const char *const stripped_extensions[] = {
      "#extension GL_EXT_shader_texture_lod",
      "#extension GL_OES_standard_derivatives",
      "#extension GL_EXT_frag_depth",
      "#extension GL_EXT_draw_buffers",
      "#extension GL_EXT_shader_framebuffer_fetch",
  };
  size_t line = 0;
  while (line < source.size()) {
    size_t line_end = source.find('\n', line);
    if (line_end == std::string::npos) line_end = source.size();
    const std::string text = trim(source.substr(line, line_end - line));
    for (const char *prefix : stripped_extensions) {
      if (text.rfind(prefix, 0) == 0) {
        Edit edit;
        edit.begin = line;
        edit.end = line_end;
        edit.text.clear();
        edit.order = 1;
        edits.push_back(edit);
      }
    }
    line = line_end + 1;
  }

  /* Insert the uniform block before the first uniform declaration (or before
   * the `#if` that encloses it, so the members stay unconditional). */
  if (!layout.block_members.empty() && info.first_uniform_offset != std::string::npos) {
    size_t insert_at = info.first_uniform_offset;
    int depth = 0;
    std::vector<size_t> starts;
    line = 0;
    while (line < info.first_uniform_offset) {
      size_t line_end = source.find('\n', line);
      if (line_end == std::string::npos || line_end > info.first_uniform_offset) {
        line_end = info.first_uniform_offset;
      }
      const std::string text = trim(source.substr(line, line_end - line));
      if (text.rfind("#if", 0) == 0) {
        starts.push_back(line);
        depth++;
      } else if (text.rfind("#endif", 0) == 0) {
        if (!starts.empty()) starts.pop_back();
        if (depth > 0) depth--;
      }
      line = line_end + 1;
    }
    if (!starts.empty()) insert_at = starts.front();
    std::string members = "layout(std140, set = ";
    members += fragment ? "3" : "1";
    members += ", binding = 0) uniform XtUniforms {\n";
    for (const GlslUniformDecl &decl : layout.block_members) {
      members += "  ";
      members += declarator_text(decl);
      members += ";\n";
    }
    members += "};\n";
    Edit block_edit;
    block_edit.begin = insert_at;
    block_edit.end = insert_at;
    block_edit.text = members;
    block_edit.order = 2;
    edits.push_back(block_edit);
  }

  int fragment_output = 0;
  for (const GlslDeclSite &site : info.sites) {
    Edit edit;
    edit.begin = site.begin;
    edit.end = site.end;
    edit.order = 3;
    switch (site.kind) {
      case GlslDeclSite::Uniform:
        edit.text.clear();
        break;
      case GlslDeclSite::Sampler: {
        int binding = 0;
        auto found = layout.samplers.find(site.decl.name);
        if (found != layout.samplers.end()) binding = found->second;
        edit.text = "layout(set = ";
        edit.text += fragment ? "2" : "0";
        edit.text += ", binding = ";
        edit.text += std::to_string(binding);
        edit.text += ") uniform ";
        edit.text += qualified_declarator_text(site.decl);
        edit.text += ";";
        break;
      }
      case GlslDeclSite::Attribute:
      case GlslDeclSite::Varying:
      case GlslDeclSite::FragmentOut: {
        int location = site.existing_location;
        if (location < 0) {
          if (site.kind == GlslDeclSite::Attribute) {
            auto found = layout.attributes.find(site.decl.name);
            if (found != layout.attributes.end()) location = found->second;
          } else if (site.kind == GlslDeclSite::Varying) {
            auto found = layout.varyings.find(site.decl.name);
            if (found != layout.varyings.end()) location = found->second;
          } else if (site.kind == GlslDeclSite::FragmentOut) {
            location = fragment_output;
          }
        }
        if (location < 0) location = 0;
        if (site.kind == GlslDeclSite::FragmentOut) {
          fragment_output = location + 1;
        }
        const char *direction = "in";
        if (site.kind == GlslDeclSite::Varying) direction = fragment ? "in" : "out";
        if (site.kind == GlslDeclSite::FragmentOut) direction = "out";
        edit.text = "layout(location = " + std::to_string(location) + ") ";
        edit.text += direction;
        edit.text += " ";
        edit.text += qualified_declarator_text(site.decl);
        edit.text += ";";
        break;
      }
    }
    edits.push_back(edit);
  }

  /* Legacy identifiers. `average` is a built-in in the Vulkan GLSL dialect
   * (three.js defines its own `average` in `common`), so rename it to avoid a
   * redeclaration error. */
  const char *const identifiers[] = {"texture2D", "textureCube", "texture2DProj",
                                     "texture2DLodEXT", "textureCubeLodEXT", "gl_FragColor",
                                     "gl_FragDepthEXT", "gl_FragData", "average"};
  const std::string replacements[] = {"texture", "texture", "textureProj", "textureLod",
                                      "textureLod", fragment_output_name, "gl_FragDepth",
                                      fragment_output_name, "xt_average"};
  size_t at = 0;
  while (at < source.size()) {
    const char c = source[at];
    if (std::isalpha((unsigned char)c) == 0 && c != '_') {
      at++;
      continue;
    }
    size_t end = at + 1;
    while (end < source.size() &&
           (std::isalnum((unsigned char)source[end]) != 0 || source[end] == '_')) {
      end++;
    }
    const std::string word = source.substr(at, end - at);
    for (size_t k = 0; k < sizeof(identifiers) / sizeof(identifiers[0]); k++) {
      if (word != identifiers[k]) continue;
      bool inside = false;
      for (const GlslDeclSite &site : info.sites) {
        if (at >= site.begin && at < site.end) inside = true;
      }
      if (inside) break;
      if (word == "gl_FragData") {
        size_t probe = end;
        while (probe < source.size() && std::isspace((unsigned char)source[probe]) != 0) probe++;
        if (probe < source.size() && source[probe] == '[') {
          size_t close = source.find(']', probe);
          if (close != std::string::npos) {
            const std::string index = trim(source.substr(probe + 1, close - probe - 1));
            if (index != "0") break;  // MRT is not supported
            Edit edit;
            edit.begin = at;
            edit.end = close + 1;
            edit.text = fragment_output_name;
            edit.order = 4;
            edits.push_back(edit);
          }
        }
        break;
      }
      Edit edit;
      edit.begin = at;
      edit.end = end;
      edit.text = replacements[k];
      edit.order = 4;
      edits.push_back(edit);
      break;
    }
    at = end;
  }

  /* Apply the non-overlapping edits in source order. */
  std::stable_sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) {
    if (a.begin != b.begin) return a.begin < b.begin;
    if (a.end != b.end) return a.end < b.end;
    return a.order < b.order;
  });
  std::string result;
  result.reserve(source.size() + 256);
  size_t cursor = 0;
  for (const Edit &edit : edits) {
    if (edit.begin < cursor) continue;  // overlaps an earlier replacement
    result.append(source, cursor, edit.begin - cursor);
    result += edit.text;
    cursor = edit.end;
  }
  result.append(source, cursor, std::string::npos);
  return result;
}

}  // namespace xtgui
