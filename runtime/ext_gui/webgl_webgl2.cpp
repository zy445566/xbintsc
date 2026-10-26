/*
 * xbintsc GUI engine — WebGL 2 vertex-array objects, queries and syncs.
 *
 * SDL_GPU has no vertex-array-object or query concept, so a VAO is a saved copy
 * of the attribute table (and its element buffer), restored into the live state
 * when bound. Queries and syncs are opaque handles; fence syncs report as
 * already signalled because every command buffer is submitted per frame.
 */

#include "webgl.h"

#include <cstring>

#include "gui_internal.h"

namespace xtgui {
namespace {

SDL_GPUFilter filter_of(int gl_filter) {
  return gl_filter == GL_NEAREST || gl_filter == GL_NEAREST_MIPMAP_NEAREST ||
                 gl_filter == GL_NEAREST_MIPMAP_LINEAR
             ? SDL_GPU_FILTER_NEAREST
             : SDL_GPU_FILTER_LINEAR;
}

SDL_GPUSamplerAddressMode address_of(int wrap) {
  switch (wrap) {
    case GL_CLAMP_TO_EDGE:
      return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    case GL_MIRRORED_REPEAT:
      return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    default:
      return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  }
}

}  // namespace

WebGLVertexArray *WebGLContext::vertex_array(uint32_t id) {
  auto found = vertex_arrays_.find(id);
  return found == vertex_arrays_.end() ? nullptr : found->second.get();
}

WebGLQuery *WebGLContext::query(uint32_t id) {
  auto found = queries_.find(id);
  return found == queries_.end() ? nullptr : found->second.get();
}

WebGLSync *WebGLContext::sync(uint32_t id) {
  auto found = syncs_.find(id);
  return found == syncs_.end() ? nullptr : found->second.get();
}

/* -- vertex arrays ------------------------------------------------------- */

uint32_t WebGLContext::create_vertex_array() {
  auto record = std::make_unique<WebGLVertexArray>();
  record->id = next_object_id_++;
  const uint32_t id = record->id;
  vertex_arrays_[id] = std::move(record);
  return id;
}

void WebGLContext::delete_vertex_array(uint32_t id) {
  if (id == 0) return;
  if (bound_vertex_array_ == id) bind_vertex_array(0);
  vertex_arrays_.erase(id);
}

void WebGLContext::save_vertex_array() {
  WebGLVertexArray *target =
      bound_vertex_array_ != 0 ? vertex_array(bound_vertex_array_) : &default_vertex_array_;
  if (target == nullptr) return;
  for (int i = 0; i < kWebGLMaxAttribs; i++) target->attribs[i] = attribs_[i];
  target->element_buffer = bound_element_;
}

void WebGLContext::load_vertex_array(const WebGLVertexArray *array) {
  for (int i = 0; i < kWebGLMaxAttribs; i++) attribs_[i] = array->attribs[i];
  bound_element_ = array->element_buffer;
}

void WebGLContext::bind_vertex_array(uint32_t id) {
  if (id != 0 && vertex_arrays_.find(id) == vertex_arrays_.end()) {
    set_error(GL_INVALID_OPERATION);
    return;
  }
  if (id == bound_vertex_array_) return;
  save_vertex_array();
  bound_vertex_array_ = id;
  load_vertex_array(id != 0 ? vertex_array(id) : &default_vertex_array_);
}

bool WebGLContext::is_vertex_array(uint32_t id) const {
  return vertex_arrays_.find(id) != vertex_arrays_.end();
}

/* -- queries and syncs --------------------------------------------------- */

uint32_t WebGLContext::create_query() {
  auto record = std::make_unique<WebGLQuery>();
  record->id = next_object_id_++;
  const uint32_t id = record->id;
  queries_[id] = std::move(record);
  return id;
}

void WebGLContext::delete_query(uint32_t id) { queries_.erase(id); }

bool WebGLContext::is_query(uint32_t id) const {
  return queries_.find(id) != queries_.end();
}

uint32_t WebGLContext::create_sync() {
  auto record = std::make_unique<WebGLSync>();
  record->id = next_object_id_++;
  const uint32_t id = record->id;
  syncs_[id] = std::move(record);
  return id;
}

void WebGLContext::delete_sync(uint32_t id) { syncs_.erase(id); }

bool WebGLContext::is_sync(uint32_t id) const {
  return syncs_.find(id) != syncs_.end();
}

uint32_t WebGLContext::uniform_block_index(uint32_t program_id,
                                           const std::string &name) const {
  auto found = programs_.find(program_id);
  if (found == programs_.end()) return 0xFFFFFFFFu;
  const std::vector<WebGLUniformBlock> &blocks = found->second->blocks;
  for (size_t i = 0; i < blocks.size(); i++) {
    if (blocks[i].name == name) return (uint32_t)i;
  }
  return 0xFFFFFFFFu;
}

WebGLUniformBlock *WebGLContext::uniform_block(uint32_t program_id, uint32_t index) {
  auto found = programs_.find(program_id);
  if (found == programs_.end() || index >= found->second->blocks.size()) return nullptr;
  return &found->second->blocks[index];
}

int WebGLContext::uniform_block_parameter(uint32_t program_id, uint32_t index,
                                          int pname) const {
  auto found = programs_.find(program_id);
  if (found == programs_.end() || index >= found->second->blocks.size()) return 0;
  const WebGLUniformBlock &block = found->second->blocks[index];
  switch (pname) {
    case GL_UNIFORM_BLOCK_BINDING:
      return block.binding;
    case GL_UNIFORM_BLOCK_DATA_SIZE:
      return block.data_size;
    case GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS:
      return block.member_count;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER:
    case GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER:
      return 1;
    default:
      return 0;
  }
}

std::string WebGLContext::uniform_block_name(uint32_t program_id, uint32_t index) const {
  auto found = programs_.find(program_id);
  if (found == programs_.end() || index >= found->second->blocks.size()) return std::string();
  return found->second->blocks[index].name;
}

void WebGLContext::uniform_block_binding(uint32_t program_id, uint32_t index, int binding) {
  WebGLUniformBlock *block = uniform_block(program_id, index);
  if (block != nullptr) block->binding = binding;
}

void WebGLContext::bind_buffer_base(int target, int index, uint32_t buffer_id,
                                    uint32_t offset, uint32_t size) {
  if (target != 0x8A11 || index < 0 || index >= 16) return;  // GL_UNIFORM_BUFFER
  uniform_bindings_[index].buffer = buffer_id;
  uniform_bindings_[index].offset = offset;
  uniform_bindings_[index].size = size;
}

uint32_t WebGLContext::uniform_buffer_binding(int index) const {
  if (index < 0 || index >= 16) return 0;
  return uniform_bindings_[index].buffer;
}

/* -- sampler objects ----------------------------------------------------- */

WebGLSampler *WebGLContext::sampler(uint32_t id) {
  auto found = samplers_.find(id);
  return found == samplers_.end() ? nullptr : found->second.get();
}

uint32_t WebGLContext::create_sampler() {
  auto record = std::make_unique<WebGLSampler>();
  record->id = next_object_id_++;
  record->min_filter = GL_NEAREST_MIPMAP_LINEAR;
  record->mag_filter = GL_LINEAR;
  record->wrap_s = GL_REPEAT;
  record->wrap_t = GL_REPEAT;
  const uint32_t id = record->id;
  samplers_[id] = std::move(record);
  return id;
}

void WebGLContext::delete_sampler(uint32_t id) {
  auto found = samplers_.find(id);
  if (found == samplers_.end()) return;
  for (uint32_t &bound : bound_samplers_) {
    if (bound == id) bound = 0;
  }
  if (found->second->gpu != nullptr && guidev::g_device != nullptr) {
    submit();
    SDL_WaitForGPUIdle(guidev::g_device);
    SDL_ReleaseGPUSampler(guidev::g_device, found->second->gpu);
  }
  samplers_.erase(found);
}

void WebGLContext::bind_sampler(int unit, uint32_t id) {
  if (unit < 0 || unit >= kWebGLMaxTextureUnits) {
    set_error(GL_INVALID_VALUE);
    return;
  }
  if (id != 0 && samplers_.find(id) == samplers_.end()) {
    set_error(GL_INVALID_OPERATION);
    return;
  }
  bound_samplers_[unit] = id;
}

bool WebGLContext::sampler_parameter(uint32_t id, int pname, int value) {
  WebGLSampler *record = sampler(id);
  if (record == nullptr) return false;
  if (pname == GL_TEXTURE_MIN_FILTER) record->min_filter = value;
  else if (pname == GL_TEXTURE_MAG_FILTER) record->mag_filter = value;
  else if (pname == GL_TEXTURE_WRAP_S) record->wrap_s = value;
  else if (pname == GL_TEXTURE_WRAP_T) record->wrap_t = value;
  if (record->gpu != nullptr && guidev::g_device != nullptr) {
    SDL_ReleaseGPUSampler(guidev::g_device, record->gpu);
    record->gpu = nullptr;
  }
  return true;
}

int WebGLContext::sampler_parameter_value(uint32_t id, int pname) {
  WebGLSampler *record = sampler(id);
  if (record == nullptr) return 0;
  if (pname == GL_TEXTURE_MIN_FILTER) return record->min_filter;
  if (pname == GL_TEXTURE_MAG_FILTER) return record->mag_filter;
  if (pname == GL_TEXTURE_WRAP_S) return record->wrap_s;
  if (pname == GL_TEXTURE_WRAP_T) return record->wrap_t;
  return 0;
}

SDL_GPUSampler *WebGLContext::sampler_gpu(WebGLSampler *record) {
  if (record == nullptr || guidev::g_device == nullptr) return nullptr;
  if (record->gpu != nullptr) return record->gpu;
  SDL_GPUSamplerCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.min_filter = filter_of(record->min_filter);
  info.mag_filter = filter_of(record->mag_filter);
  info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  info.address_mode_u = address_of(record->wrap_s);
  info.address_mode_v = address_of(record->wrap_t);
  info.address_mode_w = address_of(record->wrap_t);
  info.min_lod = 0.0f;
  /* A sampler object has no texture, so it cannot know how many levels exist;
   * textures created by this engine always carry a full mip chain. */
  info.max_lod = 1000.0f;
  record->gpu = SDL_CreateGPUSampler(guidev::g_device, &info);
  return record->gpu;
}

}  // namespace xtgui
