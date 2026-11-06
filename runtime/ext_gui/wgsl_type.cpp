/*
 * xbintsc GUI engine — WGSL type model helpers.
 */

#include "wgsl_ast.h"

#include <cstdlib>

namespace xtgui {
namespace wgsl {

TypePtr make_scalar(Scalar scalar) {
  auto type = std::make_shared<Type>();
  type->kind = Kind::Scalar;
  type->scalar = scalar;
  return type;
}

TypePtr make_vector(Scalar scalar, int width) {
  auto type = std::make_shared<Type>();
  type->kind = Kind::Vector;
  type->scalar = scalar;
  type->width = width;
  return type;
}

TypePtr make_matrix(Scalar scalar, int cols, int rows) {
  auto type = std::make_shared<Type>();
  type->kind = Kind::Matrix;
  type->scalar = scalar;
  type->cols = cols;
  type->rows = rows;
  return type;
}

TypePtr make_array(TypePtr elem, int length) {
  auto type = std::make_shared<Type>();
  type->kind = Kind::Array;
  type->elem = std::move(elem);
  type->length = length;
  return type;
}

std::string scalar_glsl(Scalar scalar) {
  switch (scalar) {
    case Scalar::Bool: return "bool";
    case Scalar::I32: return "int";
    case Scalar::U32: return "uint";
    default: return "float";
  }
}

namespace {

std::string vector_prefix(Scalar scalar) {
  switch (scalar) {
    case Scalar::Bool: return "b";
    case Scalar::I32: return "i";
    case Scalar::U32: return "u";
    default: return "";
  }
}

std::string texture_glsl(const Type &type) {
  const bool storage = type.kind == Kind::StorageTexture;
  const bool depth = type.depth;
  const TextureDim dim = type.dim;
  if (storage) {
    switch (dim) {
      case TextureDim::D1: return "image1D";
      case TextureDim::D2Array: return "image2DArray";
      case TextureDim::D3: return "image3D";
      case TextureDim::Cube:
      case TextureDim::CubeArray: return "imageCube";
      default: return "image2D";
    }
  }
  switch (dim) {
    case TextureDim::D1: return "sampler1D";
    case TextureDim::D2Array: return depth ? "sampler2DArrayShadow" : "sampler2DArray";
    case TextureDim::D3: return "sampler3D";
    case TextureDim::Cube: return depth ? "samplerCubeShadow" : "samplerCube";
    case TextureDim::CubeArray: return "samplerCubeArray";
    case TextureDim::Multisampled2D: return "sampler2DMS";
    default: break;
  }
  return depth ? "sampler2DShadow" : "sampler2D";
}

}  // namespace

std::string glsl_type(const Type &type) {
  switch (type.kind) {
    case Kind::Void: return "void";
    case Kind::Scalar: return scalar_glsl(type.scalar);
    case Kind::Vector:
      return vector_prefix(type.scalar) + "vec" + std::to_string(type.width > 0 ? type.width : 1);
    case Kind::Matrix: {
      if (type.cols == type.rows) return "mat" + std::to_string(type.cols);
      return "mat" + std::to_string(type.cols) + "x" + std::to_string(type.rows);
    }
    case Kind::Array: {
      std::string element = type.elem != nullptr ? glsl_type(*type.elem) : "float";
      return element + "[" + std::to_string(type.length) + "]";
    }
    case Kind::Struct: return type.strct != nullptr ? type.strct->name : "xt_unknown";
    case Kind::Atomic: return "uint";
    case Kind::Sampler: return "sampler2D";
    case Kind::SamplerComparison: return "sampler2DShadow";
    case Kind::Texture:
    case Kind::StorageTexture: return texture_glsl(type);
    default: return "float";
  }
}

std::string glsl_decl(const Type &type, const std::string &name) {
  if (type.kind == Kind::Array && type.elem != nullptr) {
    return glsl_decl(*type.elem, name + "[" + std::to_string(type.length) + "]");
  }
  return glsl_type(type) + " " + name;
}

std::string wgsl_type(const Type &type) {
  switch (type.kind) {
    case Kind::Void: return "void";
    case Kind::Scalar:
      switch (type.scalar) {
        case Scalar::Bool: return "bool";
        case Scalar::I32: return "i32";
        case Scalar::U32: return "u32";
        default: return "f32";
      }
    case Kind::Vector: {
      const std::string base = glsl_type(type);
      return std::string("vec") + std::to_string(type.width) +
             (base[0] == 'b' ? "b" : base[0] == 'i' ? "i" : base[0] == 'u' ? "u" : "f");
    }
    case Kind::Matrix: return "mat" + std::to_string(type.cols) + "x" + std::to_string(type.rows) + "f";
    case Kind::Array: return "array<" + (type.elem ? wgsl_type(*type.elem) : "f32") + ", " +
                             std::to_string(type.length) + ">";
    case Kind::Struct: return type.strct != nullptr ? type.strct->name : "struct";
    case Kind::Atomic: return "atomic<u32>";
    case Kind::Sampler: return "sampler";
    case Kind::SamplerComparison: return "sampler_comparison";
    case Kind::Texture: return "texture";
    case Kind::StorageTexture: return "texture_storage";
    default: return "unknown";
  }
}

TypePtr type_by_name(const Module &module, const std::string &name) {
  if (name == "f32" || name == "f16") return make_scalar(Scalar::F32);
  if (name == "i32") return make_scalar(Scalar::I32);
  if (name == "u32") return make_scalar(Scalar::U32);
  if (name == "bool") return make_scalar(Scalar::Bool);
  if (name == "sampler") {
    auto type = std::make_shared<Type>();
    type->kind = Kind::Sampler;
    return type;
  }
  if (name == "sampler_comparison") {
    auto type = std::make_shared<Type>();
    type->kind = Kind::SamplerComparison;
    return type;
  }
  /* Vector shorthands: vec3f, vec2i, vec4u (and the generic vec3<f32> form is
   * parsed directly, not looked up by name). */
  if (name.size() >= 5 && name.compare(0, 3, "vec") == 0 && name[3] >= '2' && name[3] <= '4') {
    const int width = name[3] - '0';
    const char suffix = name[4];
    Scalar scalar = Scalar::F32;
    if (suffix == 'i') scalar = Scalar::I32;
    else if (suffix == 'u') scalar = Scalar::U32;
    else if (suffix == 'b') scalar = Scalar::Bool;
    return make_vector(scalar, width);
  }
  /* Matrix shorthands: mat4x4f, mat3x2f, mat2f (= mat2x2f). */
  if (name.size() >= 5 && name.compare(0, 3, "mat") == 0) {
    int cols = 0;
    int rows = 0;
    size_t at = 3;
    while (at < name.size() && name[at] >= '0' && name[at] <= '9') {
      cols = cols * 10 + (name[at] - '0');
      at++;
    }
    if (at < name.size() && name[at] == 'x') {
      at++;
      while (at < name.size() && name[at] >= '0' && name[at] <= '9') {
        rows = rows * 10 + (name[at] - '0');
        at++;
      }
    }
    if (rows == 0) rows = cols;
    if (cols >= 2 && cols <= 4 && rows >= 2 && rows <= 4) return make_matrix(Scalar::F32, cols, rows);
  }
  for (const StructDecl &decl : module.structs) {
    if (decl.name == name) {
      auto type = std::make_shared<Type>();
      type->kind = Kind::Struct;
      type->strct = &decl;
      type->width = 1;
      return type;
    }
  }
  for (const auto &alias : module.aliases) {
    if (alias.first == name && alias.second != nullptr) {
      return std::make_shared<Type>(*alias.second);
    }
  }
  return nullptr;
}

std::string image_format_glsl(ImageFormat format) {
  switch (format) {
    case ImageFormat::Rgba8Unorm: return "rgba8";
    case ImageFormat::Rgba8Snorm: return "rgba8_snorm";
    case ImageFormat::Rgba8Uint: return "rgba8ui";
    case ImageFormat::Rgba8Sint: return "rgba8i";
    case ImageFormat::Rgba16Float: return "rgba16f";
    case ImageFormat::Rgba32Float: return "rgba32f";
    case ImageFormat::R32Float: return "r32f";
    case ImageFormat::R32Uint: return "r32ui";
    case ImageFormat::R32Sint: return "r32i";
    case ImageFormat::Rgba16Uint: return "rgba16ui";
    case ImageFormat::Rgba16Sint: return "rgba16i";
    case ImageFormat::Bgra8Unorm: return "rgba8";
    default: return "rgba8";
  }
}

}  // namespace wgsl
}  // namespace xtgui
