/*
 * xbintsc GUI engine — WebGL uniform uploads and readback.
 *
 * A program owns one std140 block; these functions write the values WebGL
 * supplies into the reflected offsets and `uniform_values` reads them back for
 * `getUniform`. Matrices are column-major and arrays stride by `array_stride`.
 */

#include "webgl.h"

#include <cstring>
#include <vector>

#include "webgl_program_internal.h"

namespace xtgui {
namespace webgl_detail {

int member_bytes(const GlslBlockMember &member) {
  if (member.is_array) {
    const int stride = member.array_stride > 0 ? member.array_stride : 16;
    return member.array_size * stride;
  }
  if (member.is_matrix) {
    const int stride = member.matrix_stride > 0 ? member.matrix_stride : 16;
    return member.columns * stride;
  }
  return member.components * 4;
}

}  // namespace webgl_detail


void WebGLContext::uniform_floats(uint32_t id, int location, int components, const double *values,
                                  int count) {
  WebGLProgram *record = program(id);
  if (record == nullptr || location < 0 || location >= (int)record->uniforms.size()) {
    set_error(GL_INVALID_OPERATION);
    return;
  }
  WebGLUniform &uniform = record->uniforms[location];
  if (uniform.sampler) {
    uniform.unit = (int)values[0];
    return;
  }
  const GlslBlockMember &member = uniform.layout;
  if (record->block_size <= 0 || member.offset + webgl_detail::member_bytes(member) > record->block_size) return;
  if (count <= 0) count = 1;
  if (member.is_matrix) {
    for (int element = 0; element < count; element++) {
      const int base = member.offset + element * member.array_stride;
      for (int column = 0; column < member.columns; column++) {
        for (int row = 0; row < member.rows; row++) {
          const float value = (float)values[(element * member.columns + column) * member.rows + row];
          const int at = base + column * member.matrix_stride + row * 4;
          if (at + 4 <= (int)record->block.size()) memcpy(&record->block[at], &value, 4);
        }
      }
    }
    record->block_dirty = true;
    return;
  }
  const int components_to_write = std::min(components, member.components);
  for (int element = 0; element < count; element++) {
    const int base = member.offset + element * (member.array_stride > 0 ? member.array_stride
                                                                       : member.components * 4);
    for (int component = 0; component < components_to_write; component++) {
      const float value = (float)values[(size_t)element * components + component];
      const int at = base + component * 4;
      if (at + 4 <= (int)record->block.size()) memcpy(&record->block[at], &value, 4);
    }
  }
  record->block_dirty = true;
}

void WebGLContext::uniform_ints(uint32_t id, int location, int components, const int *values,
                                int count) {
  WebGLProgram *record = program(id);
  if (record == nullptr || location < 0 || location >= (int)record->uniforms.size()) {
    set_error(GL_INVALID_OPERATION);
    return;
  }
  WebGLUniform &uniform = record->uniforms[location];
  if (uniform.sampler) {
    uniform.unit = values[0];
    return;
  }
  const GlslBlockMember &member = uniform.layout;
  if (record->block_size <= 0) return;
  if (count <= 0) count = 1;
  const int components_to_write = std::min(components, member.components);
  for (int element = 0; element < count; element++) {
    const int base = member.offset + element * (member.array_stride > 0 ? member.array_stride
                                                                       : member.components * 4);
    for (int component = 0; component < components_to_write; component++) {
      const int at = base + component * 4;
      if (at + 4 <= (int)record->block.size()) {
        memcpy(&record->block[at], &values[(size_t)element * components + component], 4);
      }
    }
  }
  record->block_dirty = true;
}

void WebGLContext::uniform_matrix(uint32_t id, int location, int dimensions, const double *values) {
  WebGLProgram *record = program(id);
  if (record == nullptr || location < 0 || location >= (int)record->uniforms.size()) {
    set_error(GL_INVALID_OPERATION);
    return;
  }
  WebGLUniform &uniform = record->uniforms[location];
  const GlslBlockMember &member = uniform.layout;
  if (!member.is_matrix) {
    uniform_floats(id, location, dimensions * dimensions, values, 1);
    return;
  }
  const int base = member.offset;
  for (int column = 0; column < member.columns; column++) {
    for (int row = 0; row < member.rows; row++) {
      const float value = (float)values[(size_t)column * member.rows + row];
      const int at = base + column * member.matrix_stride + row * 4;
      if (at + 4 <= (int)record->block.size()) memcpy(&record->block[at], &value, 4);
    }
  }
  record->block_dirty = true;
}

bool WebGLContext::uniform_values(uint32_t id, int location, std::vector<double> *out) {
  WebGLProgram *record = program(id);
  if (record == nullptr || location < 0 || location >= (int)record->uniforms.size()) return false;
  const WebGLUniform &uniform = record->uniforms[location];
  out->clear();
  if (uniform.sampler) {
    out->push_back(uniform.unit);
    return true;
  }
  const GlslBlockMember &member = uniform.layout;
  const int elements = member.is_array ? member.array_size : 1;
  if (member.is_matrix) {
    for (int element = 0; element < elements; element++) {
      for (int column = 0; column < member.columns; column++) {
        for (int row = 0; row < member.rows; row++) {
          float value = 0.0f;
          const int at = member.offset + element * member.array_stride +
                         column * member.matrix_stride + row * 4;
          if (at + 4 <= (int)record->block.size()) memcpy(&value, &record->block[at], 4);
          out->push_back(value);
        }
      }
    }
    return true;
  }
  for (int element = 0; element < elements; element++) {
    for (int component = 0; component < member.components; component++) {
      float value = 0.0f;
      const int at = member.offset + element * (member.array_stride > 0 ? member.array_stride
                                                                       : member.components * 4) +
                     component * 4;
      if (at + 4 <= (int)record->block.size()) memcpy(&value, &record->block[at], 4);
      out->push_back(value);
    }
  }
  return true;
}


}  // namespace xtgui
