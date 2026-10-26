/*
 * xbintsc GUI engine — glslang integration and SPIR-V reflection.
 *
 * `compile_glsl` is the only place that calls into glslang. The reflection
 * helpers read the generated module directly: WebGL has to know the exact
 * std140 offsets of the uniform block (so `uniformMatrix4fv` writes the right
 * bytes) and the active vertex attributes, and the SPIR-V already carries both
 * as decorations.
 */

#include "webgl_glsl.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

namespace xtgui {
namespace {

/* SPIR-V words are little-endian 32-bit values. */
constexpr uint16_t kOpName = 5;
constexpr uint16_t kOpMemberName = 6;
constexpr uint16_t kOpTypeBool = 20;
constexpr uint16_t kOpTypeInt = 21;
constexpr uint16_t kOpTypeFloat = 22;
constexpr uint16_t kOpTypeVector = 23;
constexpr uint16_t kOpTypeMatrix = 24;
constexpr uint16_t kOpTypeArray = 28;
constexpr uint16_t kOpTypeRuntimeArray = 29;
constexpr uint16_t kOpTypeStruct = 30;
constexpr uint16_t kOpTypePointer = 32;
constexpr uint16_t kOpConstant = 43;
constexpr uint16_t kOpVariable = 59;
constexpr uint16_t kOpDecorate = 71;
constexpr uint16_t kOpMemberDecorate = 72;

constexpr uint32_t kDecorationBlock = 2;
constexpr uint32_t kDecorationArrayStride = 6;
constexpr uint32_t kDecorationMatrixStride = 7;
constexpr uint32_t kDecorationLocation = 30;
constexpr uint32_t kDecorationOffset = 35;
constexpr uint32_t kDecorationBinding = 33;

constexpr uint32_t kStorageInput = 1;

/* GL type enums (GLES2 values). */
constexpr int kGlFloat = 0x1406;
constexpr int kGlInt = 0x1404;
constexpr int kGlBool = 0x8B56;
constexpr int kGlFloatVec2 = 0x8B50;
constexpr int kGlFloatVec3 = 0x8B51;
constexpr int kGlFloatVec4 = 0x8B52;
constexpr int kGlIntVec2 = 0x8B53;
constexpr int kGlIntVec3 = 0x8B54;
constexpr int kGlIntVec4 = 0x8B55;
constexpr int kGlFloatMat2 = 0x8B5A;
constexpr int kGlFloatMat3 = 0x8B5B;
constexpr int kGlFloatMat4 = 0x8B5C;

struct TypeInfo {
  uint16_t op = 0;
  uint32_t a = 0;  // component/element/pointee type
  uint32_t b = 0;  // count / length constant
};

struct Decor {
  uint32_t set = 0;
  uint32_t binding = 0;
  int location = -1;
  uint32_t array_stride = 0;
  bool block = false;
  bool has_descriptor_set = false;
};

struct MemberDecor {
  uint32_t offset = 0;
  uint32_t matrix_stride = 0;
};

std::string string_at(const std::vector<uint32_t> &words, size_t at) {
  std::string text;
  for (size_t i = at; i < words.size(); i++) {
    const uint32_t word = words[i];
    for (int byte = 0; byte < 4; byte++) {
      const char c = (char)((word >> (byte * 8)) & 0xFF);
      if (c == '\0') return text;
      text += c;
    }
  }
  return text;
}

struct Module {
  std::unordered_map<uint32_t, TypeInfo> types;
  std::unordered_map<uint32_t, std::vector<uint32_t>> struct_members;
  std::unordered_map<uint32_t, uint32_t> constants;
  std::unordered_map<uint32_t, std::string> names;
  std::unordered_map<uint64_t, std::string> member_names;
  std::unordered_map<uint32_t, Decor> decorations;
  std::unordered_map<uint64_t, MemberDecor> member_decorations;
  std::unordered_map<uint32_t, uint32_t> variables;  // result id -> pointer type

  bool parse(const std::vector<uint32_t> &words) {
    if (words.size() < 5 || words[0] != 0x07230203u) return false;
    size_t i = 5;
    while (i < words.size()) {
      const uint16_t count = (uint16_t)(words[i] >> 16);
      const uint16_t opcode = (uint16_t)(words[i] & 0xFFFF);
      if (count == 0 || i + count > words.size()) return false;
      const uint32_t *operands = &words[i] + 1;
      switch (opcode) {
        case kOpName:
          if (count >= 3) names[operands[0]] = string_at(words, i + 2);
          break;
        case kOpMemberName:
          if (count >= 4) member_names[((uint64_t)operands[0] << 32) | operands[1]] =
              string_at(words, i + 3);
          break;
        case kOpTypeBool:
          if (count >= 2) types[operands[0]] = {kOpTypeBool, 0, 0};
          break;
        case kOpTypeInt:
          if (count >= 2) types[operands[0]] = {kOpTypeInt, 0, 0};
          break;
        case kOpTypeFloat:
          if (count >= 2) types[operands[0]] = {kOpTypeFloat, 0, 0};
          break;
        case kOpTypeVector:
          if (count >= 3) types[operands[0]] = {kOpTypeVector, operands[1], operands[2]};
          break;
        case kOpTypeMatrix:
          if (count >= 3) types[operands[0]] = {kOpTypeMatrix, operands[1], operands[2]};
          break;
        case kOpTypeArray:
          if (count >= 3) types[operands[0]] = {kOpTypeArray, operands[1], operands[2]};
          break;
        case kOpTypeRuntimeArray:
          if (count >= 2) types[operands[0]] = {kOpTypeRuntimeArray, operands[1], 0};
          break;
        case kOpTypeStruct: {
          TypeInfo info;
          info.op = kOpTypeStruct;
          info.a = count >= 2 ? count - 2 : 0;  // member count
          types[operands[0]] = info;
          std::vector<uint32_t> members;
          /* operands[0] is the result id; member type ids start at operands[1]. */
          for (uint16_t m = 1; m + 1 < count; m++) members.push_back(operands[m]);
          struct_members[operands[0]] = std::move(members);
          break;
        }
        case kOpTypePointer:
          if (count >= 3) types[operands[0]] = {kOpTypePointer, operands[1], operands[2]};
          break;
        case kOpConstant:
          /* operands[1] is the result id, operands[2] the literal value. */
          if (count >= 3) constants[operands[1]] = operands[2];
          break;
        case kOpVariable:
          if (count >= 3) variables[operands[1]] = operands[0];
          break;
        case kOpDecorate: {
          if (count < 3) break;
          Decor &decor = decorations[operands[0]];
          if (operands[1] == kDecorationBlock) decor.block = true;
          if (operands[1] == kDecorationBinding && count >= 4) decor.binding = operands[2];
          if ((operands[1] == kDecorationLocation) && count >= 4) {
            decor.location = (int)operands[2];
          }
          if (operands[1] == 34 && count >= 4) { decor.set = operands[2]; decor.has_descriptor_set = true; }
          if (operands[1] == kDecorationArrayStride && count >= 4) {
            decor.array_stride = operands[2];
          }
          break;
        }
        case kOpMemberDecorate: {
          if (count < 4) break;
          const uint64_t key = ((uint64_t)operands[0] << 32) | operands[1];
          MemberDecor &decor = member_decorations[key];
          if (operands[2] == kDecorationOffset && count >= 5) decor.offset = operands[3];
          if (operands[2] == kDecorationMatrixStride && count >= 5) {
            decor.matrix_stride = operands[3];
          }
          break;
        }
        default:
          break;
      }
      i += count;
    }
    return true;
  }

  /** The vector size of a scalar/vector type, 0 for anything else. */
  int vector_components(uint32_t type_id, int *gl_scalar) const {
    auto found = types.find(type_id);
    if (found == types.end()) return 0;
    const TypeInfo &info = found->second;
    if (info.op == kOpTypeFloat) {
      *gl_scalar = kGlFloat;
      return 1;
    }
    if (info.op == kOpTypeInt) {
      *gl_scalar = kGlInt;
      return 1;
    }
    if (info.op == kOpTypeBool) {
      *gl_scalar = kGlBool;
      return 1;
    }
    if (info.op == kOpTypeVector) {
      auto component = types.find(info.a);
      if (component == types.end()) return 0;
      if (component->second.op == kOpTypeFloat) *gl_scalar = kGlFloat;
      if (component->second.op == kOpTypeInt) *gl_scalar = kGlInt;
      if (component->second.op == kOpTypeBool) *gl_scalar = kGlBool;
      return (int)info.b;
    }
    return 0;
  }
};

/** Fills one scalar/vector/matrix member (possibly an array of them). */
bool basic_block_member(const Module &module, uint32_t type_id, int offset,
                        const std::string &name, bool is_array, int array_size,
                        int array_stride, int matrix_stride, GlslBlockMember *member) {
  member->name = name;
  member->offset = offset;
  member->is_array = is_array;
  member->array_size = array_size;
  member->array_stride = array_stride;
  member->matrix_stride = matrix_stride;
  int scalar = 0;
  auto type = module.types.find(type_id);
  if (type != module.types.end() && type->second.op == kOpTypeMatrix) {
    int column_scalar = 0;
    const int rows = module.vector_components(type->second.a, &column_scalar);
    member->is_matrix = true;
    member->columns = (int)type->second.b;
    member->rows = rows;
    member->components = rows;
    member->scalar_type = column_scalar;
    switch (member->columns) {
      case 2:
        member->gl_type = kGlFloatMat2;
        break;
      case 3:
        member->gl_type = kGlFloatMat3;
        break;
      default:
        member->gl_type = kGlFloatMat4;
        break;
    }
    if (member->array_stride == 0 && member->is_array) {
      member->array_stride = member->columns * member->matrix_stride;
    }
    return true;
  }
  const int components = module.vector_components(type_id, &scalar);
  if (components <= 0) return false;
  member->components = components;
  member->scalar_type = scalar;
  if (components == 1) {
    member->gl_type = scalar;
  } else if (scalar == kGlFloat) {
    member->gl_type = components == 2 ? kGlFloatVec2
                                     : (components == 3 ? kGlFloatVec3 : kGlFloatVec4);
  } else if (scalar == kGlInt) {
    member->gl_type = components == 2 ? kGlIntVec2
                                     : (components == 3 ? kGlIntVec3 : kGlIntVec4);
  } else {
    member->gl_type = components == 2 ? 0x8B57 : (components == 3 ? 0x8B58 : 0x8B59);
  }
  if (member->array_stride == 0) member->array_stride = components * 4;
  return true;
}

void reflect_struct_members(const Module &module, uint32_t struct_id, int base,
                            const std::string &prefix, std::vector<GlslBlockMember> *out);

/** Walks one block member type, flattening structs/arrays of structs into
 * individual `name[i].field` entries so three.js can upload each light field. */
void reflect_block_value(const Module &module, uint32_t type_id, int offset,
                         const std::string &name, int matrix_stride,
                         std::vector<GlslBlockMember> *out) {
  auto type = module.types.find(type_id);
  if (type == module.types.end()) return;
  if (type->second.op == kOpTypeArray) {
    auto length = module.constants.find(type->second.b);
    const int count = length == module.constants.end() ? 0 : (int)length->second;
    int stride = 0;
    auto decor = module.decorations.find(type_id);
    if (decor != module.decorations.end()) stride = (int)decor->second.array_stride;
    const uint32_t element = type->second.a;
    auto element_type = module.types.find(element);
    if (element_type != module.types.end() && element_type->second.op == kOpTypeStruct) {
      for (int i = 0; i < count; i++) {
        reflect_struct_members(module, element, offset + i * stride,
                               name + "[" + std::to_string(i) + "]", out);
      }
      return;
    }
    GlslBlockMember member;
    if (basic_block_member(module, element, offset, name, true, count, stride, matrix_stride,
                           &member)) {
      out->push_back(member);
    }
    return;
  }
  if (type->second.op == kOpTypeStruct) {
    reflect_struct_members(module, type_id, offset, name, out);
    return;
  }
  GlslBlockMember member;
  if (basic_block_member(module, type_id, offset, name, false, 1, 0, matrix_stride, &member)) {
    out->push_back(member);
  }
}

void reflect_struct_members(const Module &module, uint32_t struct_id, int base,
                            const std::string &prefix, std::vector<GlslBlockMember> *out) {
  auto members = module.struct_members.find(struct_id);
  if (members == module.struct_members.end()) return;
  for (size_t index = 0; index < members->second.size(); index++) {
    const uint64_t key = ((uint64_t)struct_id << 32) | (uint32_t)index;
    auto name = module.member_names.find(key);
    auto decor = module.member_decorations.find(key);
    if (name == module.member_names.end() || decor == module.member_decorations.end()) continue;
    const std::string child = prefix.empty() ? name->second : prefix + "." + name->second;
    reflect_block_value(module, members->second[index], base + (int)decor->second.offset, child,
                        (int)decor->second.matrix_stride, out);
  }
}

}  // namespace

bool compile_glsl(const std::string &source, bool fragment, std::vector<uint32_t> *spirv,
                  std::string *log) {
  static const bool initialized = glslang::InitializeProcess();
  if (!initialized) {
    *log = "glslang initialization failed";
    return false;
  }
  const EShLanguage stage = fragment ? EShLangFragment : EShLangVertex;
  glslang::TShader shader(stage);
  const char *text = source.c_str();
  shader.setStrings(&text, 1);
  shader.setEntryPoint("main");
  shader.setSourceEntryPoint("main");
  shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 310);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
  if (!shader.parse(GetDefaultResources(), 310, false, EShMsgDefault)) {
    *log = std::string("vertex/fragment parse failed:\n") + shader.getInfoLog() +
           shader.getInfoDebugLog();
    return false;
  }
  glslang::TProgram program;
  program.addShader(&shader);
  if (!program.link(EShMsgDefault)) {
    *log = std::string("link failed:\n") + program.getInfoLog() + program.getInfoDebugLog();
    return false;
  }
  spirv->clear();
  glslang::GlslangToSpv(*program.getIntermediate(stage), *spirv);
  return !spirv->empty();
}

bool reflect_uniform_block(const std::vector<uint32_t> &spirv, std::vector<GlslBlockMember> *out) {
  out->clear();
  Module module;
  if (!module.parse(spirv)) return false;

  /* Find the uniform block this engine emitted: a Block struct reached through
   * a variable that carries an explicit descriptor set. The vertex stage also
   * has the built-in `gl_PerVertex` Block (no descriptor set), which must not
   * be mistaken for the std140 uniform block. */
  uint32_t struct_id = 0;
  for (const auto &entry : module.variables) {
    auto type = module.types.find(entry.second);
    if (type == module.types.end() || type->second.op != kOpTypePointer) continue;
    auto pointee = module.types.find(type->second.b);
    if (pointee == module.types.end() || pointee->second.op != kOpTypeStruct) continue;
    auto decor = module.decorations.find(type->second.b);
    if (decor == module.decorations.end() || !decor->second.block) continue;
    auto variable_decor = module.decorations.find(entry.first);
    if (variable_decor == module.decorations.end() || !variable_decor->second.has_descriptor_set) {
      continue;
    }
    struct_id = type->second.b;
    break;
  }
  if (struct_id == 0) return true;  // no uniform block: nothing to reflect

  auto members = module.struct_members.find(struct_id);
  if (members != module.struct_members.end()) {
    for (size_t index = 0; index < members->second.size(); index++) {
      const uint64_t key = ((uint64_t)struct_id << 32) | (uint32_t)index;
      auto name = module.member_names.find(key);
      auto decor = module.member_decorations.find(key);
      if (name == module.member_names.end() || decor == module.member_decorations.end()) continue;
      reflect_block_value(module, members->second[index], (int)decor->second.offset, name->second,
                          (int)decor->second.matrix_stride, out);
    }
  }
  return true;
}

bool reflect_attributes(const std::vector<uint32_t> &spirv,
                        std::unordered_map<std::string, int> *out) {
  out->clear();
  Module module;
  if (!module.parse(spirv)) return false;
  for (const auto &entry : module.variables) {
    auto pointer = module.types.find(entry.second);
    if (pointer == module.types.end() || pointer->second.op != kOpTypePointer) continue;
    if (pointer->second.a != kStorageInput) continue;
    auto name = module.names.find(entry.first);
    auto decor = module.decorations.find(entry.first);
    if (name == module.names.end() || decor == module.decorations.end()) continue;
    if (decor->second.location < 0) continue;
    (*out)[name->second] = decor->second.location;
  }
  return true;
}

}  // namespace xtgui