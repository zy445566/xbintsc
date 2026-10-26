/*
 * xbintsc GUI engine — WebGL shaders, programs, linking and uniforms.
 *
 * `compileShader` performs a standalone translation so `COMPILE_STATUS` and the
 * info log work before linking. `linkProgram` then translates both stages again
 * with one shared attribute/varying location map, compiles them with glslang,
 * reflects the std140 block out of the SPIR-V and creates the SDL_GPU shaders.
 */

#include "webgl.h"

#include <algorithm>
#include <cstring>

#include "gui_internal.h"
#include "webgl_program_internal.h"

namespace xtgui {
namespace {

int gl_type_of(const std::string &type) {
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
  return 0;
}

/** How many consecutive attribute/varying locations a type consumes. */
int location_slots(const std::string &type) {
  if (type == "mat2") return 2;
  if (type == "mat3") return 3;
  if (type == "mat4") return 4;
  return 1;
}

/** Merge uniforms declared by both stages, in declaration order. */
bool merge_uniforms(const GlslShaderInfo &vs, const GlslShaderInfo &fs,
                    std::vector<GlslUniformDecl> *out, std::string *error) {
  for (const GlslShaderInfo *info : {&vs, &fs}) {
    for (const GlslUniformDecl &decl : info->uniforms) {
      auto found = std::find_if(out->begin(), out->end(),
                                [&](const GlslUniformDecl &existing) {
                                  return existing.name == decl.name;
                                });
      if (found == out->end()) {
        out->push_back(decl);
      } else if (found->type != decl.type) {
        *error = "uniform '" + decl.name + "' is declared with two different types";
        return false;
      }
    }
  }
  return true;
}

void assign_locations(const std::vector<GlslUniformDecl> &decls,
                      const std::unordered_map<std::string, int> &overrides,
                      std::unordered_map<std::string, int> *out) {
  int next = 0;
  for (const GlslUniformDecl &decl : decls) {
    auto override_it = overrides.find(decl.name);
    if (override_it != overrides.end()) {
      (*out)[decl.name] = override_it->second;
      next = std::max(next, override_it->second + location_slots(decl.type));
      continue;
    }
    while (true) {
      bool used = false;
      for (const auto &entry : *out) {
        const int first = entry.second;
        if (next >= first && next < first + location_slots(decl.type)) used = true;
      }
      if (!used) break;
      next++;
    }
    (*out)[decl.name] = next;
    next += location_slots(decl.type);
  }
}


}  // namespace

/* -- shaders ------------------------------------------------------------- */

uint32_t WebGLContext::create_shader(int type) {
  if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) {
    set_error(GL_INVALID_ENUM);
    return 0;
  }
  auto record = std::make_unique<WebGLShader>();
  record->id = next_object_id_++;
  record->type = type;
  const uint32_t id = record->id;
  shaders_[id] = std::move(record);
  return id;
}

void WebGLContext::delete_shader(uint32_t id) {
  auto found = shaders_.find(id);
  if (found == shaders_.end()) return;
  if (found->second->attachments > 0) {
    found->second->delete_requested = true;
    return;
  }
  shaders_.erase(found);
}

void WebGLContext::shader_source(uint32_t id, const std::string &source) {
  WebGLShader *record = shader(id);
  if (record == nullptr) return;
  record->source = source;
  record->compiled = false;
  record->log.clear();
}

bool WebGLContext::compile_shader(uint32_t id) {
  WebGLShader *record = shader(id);
  if (record == nullptr) return false;
  const bool fragment = record->type == GL_FRAGMENT_SHADER;
  GlslShaderInfo info;
  scan_glsl(record->source, fragment, &info);
  if (!info.unsupported.empty()) {
    record->log = info.unsupported;
    record->compiled = false;
    return false;
  }
  GlslLinkLayout layout;
  assign_locations(info.attributes, {}, &layout.attributes);
  assign_locations(info.varyings, {}, &layout.varyings);
  assign_locations(info.samplers, {}, &layout.samplers);
  layout.block_members = info.uniforms;
  const std::string source = rewrite_glsl(record->source, fragment, layout, info);
  std::vector<uint32_t> spirv;
  record->compiled = compile_glsl(source, fragment, &spirv, &record->log);
  if (!record->compiled) {
  }
  return record->compiled;
}

std::string WebGLContext::shader_info_log(uint32_t id) {
  WebGLShader *record = shader(id);
  return record != nullptr ? record->log : std::string();
}

bool WebGLContext::shader_compiled(uint32_t id) {
  WebGLShader *record = shader(id);
  return record != nullptr && record->compiled;
}

std::string WebGLContext::shader_source_text(uint32_t id) {
  WebGLShader *record = shader(id);
  return record != nullptr ? record->source : std::string();
}

/* -- programs ------------------------------------------------------------ */

uint32_t WebGLContext::create_program() {
  auto record = std::make_unique<WebGLProgram>();
  record->id = next_object_id_++;
  const uint32_t id = record->id;
  programs_[id] = std::move(record);
  return id;
}

void WebGLContext::release_program_pipelines(WebGLProgram *program) {
  if (program == nullptr || guidev::g_device == nullptr) return;
  for (SDL_GPUGraphicsPipeline *pipeline : program->pipelines) {
    SDL_ReleaseGPUGraphicsPipeline(guidev::g_device, pipeline);
  }
  program->pipelines.clear();
  program->pipeline_cache.clear();
  if (program->vs_shader != nullptr) SDL_ReleaseGPUShader(guidev::g_device, program->vs_shader);
  if (program->fs_shader != nullptr) SDL_ReleaseGPUShader(guidev::g_device, program->fs_shader);
  program->vs_shader = nullptr;
  program->fs_shader = nullptr;
}

void WebGLContext::delete_program(uint32_t id) {
  auto found = programs_.find(id);
  if (found == programs_.end()) return;
  if (current_program_ == id) current_program_ = 0;
  WebGLProgram *record = found->second.get();
  if (record->vs != 0) detach_shader(id, record->vs);
  if (record->fs != 0) detach_shader(id, record->fs);
  if (guidev::g_device != nullptr) {
    submit(); SDL_WaitForGPUIdle(guidev::g_device);
    release_program_pipelines(record);
  }
  programs_.erase(found);
}

bool WebGLContext::attach_shader(uint32_t program_id, uint32_t shader_id) {
  WebGLProgram *record = program(program_id);
  WebGLShader *shader_record = shader(shader_id);
  if (record == nullptr || shader_record == nullptr) {
    set_error(GL_INVALID_OPERATION);
    return false;
  }
  uint32_t *slot = shader_record->type == GL_VERTEX_SHADER ? &record->vs : &record->fs;
  if (*slot == shader_id) return true;
  if (shader_record->type != GL_VERTEX_SHADER && shader_record->type != GL_FRAGMENT_SHADER) {
    return false;
  }
  *slot = shader_id;
  shader_record->attachments++;
  return true;
}

bool WebGLContext::detach_shader(uint32_t program_id, uint32_t shader_id) {
  WebGLProgram *record = program(program_id);
  WebGLShader *shader_record = shader(shader_id);
  if (record == nullptr || shader_record == nullptr) return false;
  if (record->vs == shader_id) record->vs = 0;
  if (record->fs == shader_id) record->fs = 0;
  if (shader_record->attachments > 0) shader_record->attachments--;
  if (shader_record->attachments == 0 && shader_record->delete_requested) {
    shaders_.erase(shader_id);
  }
  return true;
}

bool WebGLContext::link_program(uint32_t id) {
  WebGLProgram *record = program(id);
  if (record == nullptr || record->vs == 0 || record->fs == 0) {
    if (record != nullptr) record->log = "link needs one vertex and one fragment shader";
    set_error(GL_INVALID_OPERATION);
    return false;
  }
  WebGLShader *vertex = shader(record->vs);
  WebGLShader *fragment = shader(record->fs);
  if (vertex == nullptr || fragment == nullptr || !vertex->compiled || !fragment->compiled) {
    record->log = "link needs both shaders to compile successfully";
    record->linked = false;
    return false;
  }

  GlslShaderInfo vs_info;
  GlslShaderInfo fs_info;
  scan_glsl(vertex->source, false, &vs_info);
  scan_glsl(fragment->source, true, &fs_info);
  if (!vs_info.unsupported.empty() || !fs_info.unsupported.empty()) {
    record->log = vs_info.unsupported.empty() ? fs_info.unsupported : vs_info.unsupported;
    record->linked = false;
    return false;
  }

  std::vector<GlslUniformDecl> block_members;
  if (!merge_uniforms(vs_info, fs_info, &block_members, &record->log)) {
    record->linked = false;
    return false;
  }

  GlslLinkLayout vs_layout;
  GlslLinkLayout fs_layout;
  vs_layout.block_members = block_members;
  fs_layout.block_members = block_members;
  assign_locations(vs_info.attributes, record->attrib_bindings, &vs_layout.attributes);
  fs_layout.attributes = vs_layout.attributes;
  assign_locations(vs_info.varyings, {}, &vs_layout.varyings);
  fs_layout.varyings = vs_layout.varyings;
  assign_locations(fs_info.samplers, {}, &fs_layout.samplers);
  assign_locations(vs_info.samplers, {}, &vs_layout.samplers);

  /* A fragment input with no vertex output is undefined in Vulkan, but a
   * material may declare one conditionally and never read it (three.js does
   * this for `vNormal` on unlit materials). Give it a location and let the
   * compiler drop it rather than failing the whole program. */
  for (const GlslUniformDecl &varying : fs_info.varyings) {
    if (vs_layout.varyings.find(varying.name) == vs_layout.varyings.end()) {
      int next = 0;
      for (const auto &entry : vs_layout.varyings) {
        if (entry.second >= next) next = entry.second + 1;
      }
      vs_layout.varyings[varying.name] = next;
    }
  }
  fs_layout.varyings = vs_layout.varyings;

  const std::string vs_source = rewrite_glsl(vertex->source, false, vs_layout, vs_info);
  const std::string fs_source = rewrite_glsl(fragment->source, true, fs_layout, fs_info);
  std::vector<uint32_t> vs_spirv;
  std::vector<uint32_t> fs_spirv;
  std::string log;
  if (!compile_glsl(vs_source, false, &vs_spirv, &log)) {
    record->log = "vertex shader:\n" + log;
    record->linked = false;
    return false;
  }
  if (!compile_glsl(fs_source, true, &fs_spirv, &log)) {
    record->log = "fragment shader:\n" + log;
    record->linked = false;
    return false;
  }

  /* Reflect the std140 block (members survive even when unused). */
  std::vector<GlslBlockMember> members;
  reflect_uniform_block(vs_spirv, &members);
  std::vector<GlslBlockMember> fragment_members;
  reflect_uniform_block(fs_spirv, &fragment_members);
  if (members.empty()) members = fragment_members;

  record->uniforms.clear();
  record->uniform_index.clear();
  /* Only attributes that survived preprocessing and reached the SPIR-V are
   * active. three.js emits many conditional `#ifdef USE_…` attribute
   * declarations, so the source scan over-reports; use the reflection and keep
   * the declaration order from the source for stable `getActiveAttrib` order. */
  std::unordered_map<std::string, int> active_attributes;
  reflect_attributes(vs_spirv, &active_attributes);
  if (!active_attributes.empty()) {
    std::vector<std::pair<int, GlslUniformDecl>> ordered;
    for (const GlslUniformDecl &decl : vs_info.attributes) {
      auto found = active_attributes.find(decl.name);
      if (found != active_attributes.end()) ordered.push_back({found->second, decl});
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const std::pair<int, GlslUniformDecl> &a,
                 const std::pair<int, GlslUniformDecl> &b) { return a.first < b.first; });
    record->attribs.clear();
    for (const auto &entry : ordered) record->attribs.push_back(entry.second);
    record->attrib_locations = active_attributes;
  } else {
    record->attribs = vs_info.attributes;
    record->attrib_locations = vs_layout.attributes;
  }
  record->vs_sampler_uniforms.clear();
  record->fs_sampler_uniforms.clear();

  for (const GlslBlockMember &member : members) {
    WebGLUniform uniform;
    uniform.name = member.name;
    uniform.layout = member;
    record->uniform_index[member.name] = (int)record->uniforms.size();
    record->uniforms.push_back(uniform);
  }
  int block_size = 0;
  for (const WebGLUniform &uniform : record->uniforms) {
    block_size = std::max(block_size, uniform.layout.offset + webgl_detail::member_bytes(uniform.layout));
  }
  record->block_size = (block_size + 15) & ~15;
  record->block.assign(record->block_size, 0);
  record->block_dirty = true;

  auto add_sampler = [](WebGLProgram *program, std::vector<int> *stage,
                        const GlslUniformDecl &decl) {
    int index = -1;
    auto found = program->uniform_index.find(decl.name);
    if (found != program->uniform_index.end()) {
      index = found->second;
    } else {
      WebGLUniform uniform;
      uniform.name = decl.name;
      uniform.sampler = true;
      uniform.unit = 0;
      uniform.layout.gl_type = gl_type_of(decl.type);
      index = (int)program->uniforms.size();
      program->uniform_index[decl.name] = index;
      program->uniforms.push_back(uniform);
    }
    stage->push_back(index);
  };
  for (const GlslUniformDecl &decl : vs_info.samplers) add_sampler(record, &record->vs_sampler_uniforms, decl);
  for (const GlslUniformDecl &decl : fs_info.samplers) add_sampler(record, &record->fs_sampler_uniforms, decl);

  /* (Re)create the SDL shaders. */
  if (guidev::g_device == nullptr) {
    record->log = "no GPU device";
    record->linked = false;
    return false;
  }
  submit(); SDL_WaitForGPUIdle(guidev::g_device);
  release_program_pipelines(record);

  SDL_GPUShaderCreateInfo vs_create;
  memset(&vs_create, 0, sizeof(vs_create));
  vs_create.code = (const Uint8 *)vs_spirv.data();
  vs_create.code_size = vs_spirv.size() * sizeof(uint32_t);
  vs_create.entrypoint = "main";
  vs_create.format = SDL_GPU_SHADERFORMAT_SPIRV;
  vs_create.stage = SDL_GPU_SHADERSTAGE_VERTEX;
  record->vs_has_block = !vs_info.uniforms.empty();
  vs_create.num_uniform_buffers = record->vs_has_block ? 1 : 0;
  vs_create.num_samplers = (Uint32)record->vs_sampler_uniforms.size();
  record->vs_shader = SDL_CreateGPUShader(guidev::g_device, &vs_create);

  SDL_GPUShaderCreateInfo fs_create;
  memset(&fs_create, 0, sizeof(fs_create));
  fs_create.code = (const Uint8 *)fs_spirv.data();
  fs_create.code_size = fs_spirv.size() * sizeof(uint32_t);
  fs_create.entrypoint = "main";
  fs_create.format = SDL_GPU_SHADERFORMAT_SPIRV;
  fs_create.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  record->fs_has_block = !fs_info.uniforms.empty();
  fs_create.num_uniform_buffers = record->fs_has_block ? 1 : 0;
  fs_create.num_samplers = (Uint32)record->fs_sampler_uniforms.size();
  record->fs_shader = SDL_CreateGPUShader(guidev::g_device, &fs_create);
  if (record->vs_shader == nullptr || record->fs_shader == nullptr) {
    record->log = std::string("SDL_GPU shader creation failed: ") + SDL_GetError();
    release_program_pipelines(record);
    record->linked = false;
    return false;
  }
  record->linked = true;
  record->log.clear();
  return true;
}

void WebGLContext::use_program(uint32_t id) {
  if (id != 0 && (programs_.find(id) == programs_.end() || !programs_[id]->linked)) {
    set_error(GL_INVALID_OPERATION);
    return;
  }
  current_program_ = id;
}

bool WebGLContext::program_linked(uint32_t id) {
  WebGLProgram *record = program(id);
  return record != nullptr && record->linked;
}

std::string WebGLContext::program_info_log(uint32_t id) {
  WebGLProgram *record = program(id);
  return record != nullptr ? record->log : std::string();
}

std::vector<uint32_t> WebGLContext::attached_shaders(uint32_t id) {
  std::vector<uint32_t> result;
  WebGLProgram *record = program(id);
  if (record == nullptr) return result;
  if (record->vs != 0) result.push_back(record->vs);
  if (record->fs != 0) result.push_back(record->fs);
  return result;
}

int WebGLContext::active_uniform_count(uint32_t id) {
  WebGLProgram *record = program(id);
  return record != nullptr ? (int)record->uniforms.size() : 0;
}

int WebGLContext::active_attrib_count(uint32_t id) {
  WebGLProgram *record = program(id);
  return record != nullptr ? (int)record->attribs.size() : 0;
}

const WebGLUniform *WebGLContext::active_uniform(uint32_t id, int index) {
  WebGLProgram *record = program(id);
  if (record == nullptr || index < 0 || index >= (int)record->uniforms.size()) return nullptr;
  return &record->uniforms[index];
}

const GlslUniformDecl *WebGLContext::active_attrib(uint32_t id, int index) {
  WebGLProgram *record = program(id);
  if (record == nullptr || index < 0 || index >= (int)record->attribs.size()) return nullptr;
  return &record->attribs[index];
}

int WebGLContext::uniform_location(uint32_t id, const std::string &name) {
  WebGLProgram *record = program(id);
  if (record == nullptr) return -1;
  auto found = record->uniform_index.find(name);
  if (found != record->uniform_index.end()) return found->second;
  const size_t bracket = name.find('[');
  if (bracket != std::string::npos) {
    found = record->uniform_index.find(name.substr(0, bracket));
    if (found != record->uniform_index.end()) return found->second;
  }
  return -1;
}

int WebGLContext::attrib_location(uint32_t id, const std::string &name) {
  WebGLProgram *record = program(id);
  if (record == nullptr) return -1;
  auto found = record->attrib_locations.find(name);
  return found == record->attrib_locations.end() ? -1 : found->second;
}

void WebGLContext::bind_attrib_location(uint32_t id, int index, const std::string &name) {
  WebGLProgram *record = program(id);
  if (record == nullptr || index < 0 || index >= kWebGLMaxAttribs) return;
  record->attrib_bindings[name] = index;
}

/* -- uniform uploads ----------------------------------------------------- */

}  // namespace xtgui