/*
 * xbintsc GUI engine — GLSL ES for WebGL.
 *
 * WebGL shaders reach `shaderSource` as GLSL ES 1.00/3.00 text, while SDL_GPU's
 * Vulkan backend consumes SPIR-V. There is no GLSL compiler in SDL_GPU, so this
 * module does two things:
 *
 *   1. rewrites a WebGL shader into Vulkan-flavoured GLSL ES 3.10
 *      (`attribute` -> `in`, `varying` -> in/out with explicit locations,
 *      `gl_FragColor` -> an output, non-sampler uniforms -> one std140 block,
 *      samplers -> explicit descriptor set/binding, `texture2D` -> `texture`);
 *   2. compiles it with glslang and reflects the resulting SPIR-V, which yields
 *      the exact std140 offsets, array strides and matrix strides of the
 *      uniform block plus the active attributes and sampler bindings.
 *
 * Reflection is the source of truth for layout: the shader text may size an
 * array with a macro, but glslang has already resolved it by the time we look
 * at the block members.
 */
#ifndef XT_GUI_WEBGL_GLSL_H
#define XT_GUI_WEBGL_GLSL_H

#include <cstdint>
#include <string>

#include "webgl_glsl_tokens.h"
#include <unordered_map>
#include <utility>
#include <vector>

namespace xtgui {

/** One `uniform` declaration found in a shader source. */
struct GlslUniformDecl {
  std::string type;       // `mat4`, `vec3`, `sampler2D`, …
  std::string name;
  std::string array;      // `[4]` (raw text, may contain a macro)
  std::string precision;  // `highp` / `mediump` / `lowp` / empty
  bool sampler = false;
  /** Extra qualifiers (`layout(...)`) that were already present; empty for a
   * WebGL 1 source. */
  std::string extra;
};

/** One member of a user-declared uniform block. */
struct GlslBlockMemberDecl {
  std::string type;
  std::string name;
  std::string array;
};

/** A user-declared `uniform Block { … } instance;`. The rewriter merges its
 * members into the engine block under `__ub_<instance>_<member>` names and
 * rewrites references, because SDL_GPU has no bindable uniform buffers. */
struct GlslUniformBlock {
  std::string name;
  std::string instance;
  std::vector<GlslBlockMemberDecl> members;
  size_t begin = 0;
  size_t end = 0;
  bool conditional = false;
};

/** One `instance.member` reference to rename. */
struct GlslRename {
  size_t begin = 0;
  size_t end = 0;
  std::string text;
};

/** Where one declaration sits in the source text, and what it declares. */
struct GlslDeclSite {
  enum Kind { Uniform, Sampler, Attribute, Varying, FragmentOut };
  Kind kind = Uniform;
  size_t begin = 0;  // byte offset of the first token (`layout` included)
  size_t end = 0;    // byte offset just past the `;`
  GlslUniformDecl decl;
  int existing_location = -1;  // from a `layout(location = …)` qualifier
  /** True when a preprocessor `#if`/`#ifdef` encloses the declaration. */
  bool conditional = false;
};

/** The declarations a shader contributes to a program. */
struct GlslShaderInfo;

/** Parse a user uniform block at `uniform_index`, merging its members. Returns
 * true when a block declaration was consumed; `after` is the next token. */
bool scan_uniform_block(const std::vector<GlslToken> &tokens, size_t uniform_index,
                        size_t pending_layout, bool conditional, GlslShaderInfo *info,
                        size_t *after, size_t *first_uniform_any);

struct GlslShaderInfo {
  std::vector<GlslUniformDecl> uniforms;   // non-sampler
  std::vector<GlslUniformDecl> samplers;
  std::vector<GlslUniformDecl> attributes; // vertex only
  std::vector<GlslUniformDecl> varyings;
  /** Every top-level declaration, in source order, for the rewriter. */
  std::vector<GlslDeclSite> sites;
  /** Active top-level `struct Name { … };` definitions, hoisted before the
   * uniform block because the block may name those types (three.js lights). */
  std::vector<std::pair<size_t, size_t>> structs;
  /** The text of those definitions, shared across stages at link time. */
  std::vector<std::string> struct_definitions;
  /** User uniform blocks, merged into the single engine block at link time. */
  std::vector<GlslUniformBlock> blocks;
  /** References the rewriter must spell with the prefixed member name. */
  std::vector<GlslRename> renames;
  /** True when the source writes `gl_FragColor` / `gl_FragData[0]`. */
  bool legacy_fragment_output = false;
  /** True when the source declared its own `out` variable (ES 3.00). */
  bool has_fragment_output = false;
  /** Byte offset of the first non-sampler uniform declaration (block insertion
   * point); `npos` when the shader has none. */
  size_t first_uniform_offset = std::string::npos;
  /** A declaration this engine cannot translate (struct uniform, block, …). */
  std::string unsupported;
};

/** Locations and bindings assigned at link time. */
struct GlslLinkLayout {
  std::unordered_map<std::string, int> attributes; // name -> location
  std::unordered_map<std::string, int> varyings;   // name -> location
  /** stage-local sampler binding: name -> binding index inside the stage. */
  std::unordered_map<std::string, int> samplers;
  /** Merged, declaration-ordered non-sampler uniforms (shared block members). */
  std::vector<GlslUniformDecl> block_members;
};

/** Expand three.js's `#pragma unroll_loop_start` loops into constant indices. */
std::string unroll_glsl_pragmas(const std::string &source);

/** Scan `source` for the declarations WebGL needs to know about. */
void scan_glsl(const std::string &source, bool fragment, GlslShaderInfo *info);

/** Rewrite `source` into Vulkan GLSL ES 3.10 (`layout` added everywhere). */
std::string rewrite_glsl(const std::string &source, bool fragment, const GlslLinkLayout &layout,
                         const GlslShaderInfo &info);

/** Compile a rewritten shader to SPIR-V. Returns false and fills `log`. */
bool compile_glsl(const std::string &source, bool fragment, std::vector<uint32_t> *spirv,
                  std::string *log);

/** One std140 block member, reflected from SPIR-V. */
struct GlslBlockMember {
  std::string name;
  int gl_type = 0;       // GL_FLOAT, GL_FLOAT_VEC4, GL_FLOAT_MAT4, GL_INT_VEC2, …
  int components = 0;    // 1..4
  bool is_matrix = false;
  int columns = 0;       // matrix columns; 0 for non-matrix members
  int rows = 0;
  bool is_array = false;
  int array_size = 1;
  int offset = 0;         // byte offset of the member in the block
  int array_stride = 0;   // byte stride between array elements
  int matrix_stride = 0;  // byte stride between matrix columns
  int scalar_type = 0;    // GL_FLOAT / GL_INT / GL_BOOL
};

/** Reflect the (single) uniform block of a compiled shader. */
bool reflect_uniform_block(const std::vector<uint32_t> &spirv, std::vector<GlslBlockMember> *out);

/** Reflect `in` variable locations (vertex attributes) from SPIR-V. */
bool reflect_attributes(const std::vector<uint32_t> &spirv,
                        std::unordered_map<std::string, int> *out);

}  // namespace xtgui

#endif /* XT_GUI_WEBGL_GLSL_H */