/*
 * xbintsc GUI engine — WGSL calls, texture operations and constructors.
 */

#include <algorithm>
#include <string>
#include <vector>

#include "wgsl_emit.h"

namespace xtgui {
namespace wgsl {
namespace {

std::string ident_of(const ExprPtr &node) {
  return node != nullptr && node->kind == Expr::K::Ident ? node->text : std::string();
}

const char *compare_function(const std::string &name) {
  if (name == "textureSampleCompare" || name == "textureSampleCompareLevel") return "texture";
  return nullptr;
}

std::string coord_width(int width) {
  if (width <= 1) return "int";
  if (width == 2) return "ivec2";
  return "ivec3";
}

bool is_pass_through_builtin(const std::string &name) {
  static const char *const kNames[] = {
      "abs",       "acos",      "acosh",     "asin",      "asinh",      "atan",
      "atanh",     "ceil",      "clamp",     "cos",       "cosh",       "cross",
      "degrees",   "determinant", "distance", "dot",      "exp",        "exp2",
      "faceForward", "floor",   "fma",       "fract",     "fwidth",     "ldexp",
      "length",    "log",       "log2",      "max",       "min",        "mix",
      "modf",      "normalize", "pow",       "radians",   "reflect",    "refract",
      "roundEven", "sign",      "sin",       "sinh",      "smoothstep", "sqrt",
      "step",      "tan",       "tanh",      "transpose", "trunc",      "inverse",
      "bitCount",  "bitfieldReverse", "findMSB", "findLSB", "bitfieldExtract",
      "bitfieldInsert", "packHalf2x16", "unpackHalf2x16", "packUnorm2x16"};
  for (const char *candidate : kNames) {
    if (name == candidate) return true;
  }
  return false;
}

}  // namespace

std::string Emitter::call_expr(const Expr &node) {
  std::vector<std::string> args;
  args.reserve(node.args.size());
  for (const ExprPtr &arg : node.args) args.push_back(arg != nullptr ? expr(*arg) : "0");
  auto joined = [&](size_t from) {
    std::string text;
    for (size_t i = from; i < args.size(); i++) {
      if (i > from) text += ", ";
      text += args[i];
    }
    return text;
  };

  /* Method call: `array.length()`. */
  if (!node.text.empty() && node.text[0] == '.') {
    if (node.text == ".length") {
      const TypePtr base = node.args.empty() ? nullptr : type_of(*node.args[0]);
      if (base != nullptr && base->kind == Kind::Array) {
        if (base->length > 0) return std::to_string(base->length);
        return "uint(" + args[0] + ".length())";
      }
      return std::to_string(base != nullptr ? base->width : 1);
    }
    fail("unsupported WGSL method '" + node.text + "'");
    return "0";
  }

  /* Type constructors (vec3f(…), mat4x4f(…), S(…)). */
  if (TypePtr constructed = type_by_name(module_, node.text)) {
    return glsl_type(*constructed) + "(" + joined(0) + ")";
  }

  /* User functions. */
  if (const FuncDecl *function = find_function(node.text)) {
    return "xg_" + function->name + "(" + joined(0) + ")";
  }

  const std::string &name = node.text;

  /* -- textures ---------------------------------------------------------- */
  if (name.compare(0, 13, "textureSample") == 0) {
    if (node.args.size() < 3) {
      fail("textureSample needs a texture, a sampler and coordinates");
      return "vec4(0.0)";
    }
    const std::string texture = ident_of(node.args[0]);
    const std::string sampler = ident_of(node.args[1]);
    const int slot = pair_slot_for(texture, sampler);
    if (slot < 0) {
      fail("textureSample uses a texture or sampler that is not a module-level resource");
      return "vec4(0.0)";
    }
    const std::string pair = "xg_tex" + std::to_string(slot);
    const GlobalDecl *decl = find_global(texture);
    const TypePtr type = decl != nullptr ? decl->type : nullptr;
    const TextureDim dim = type != nullptr ? type->dim : TextureDim::D2;
    std::string coords = args[2];
    if (dim == TextureDim::D2Array && args.size() >= 4) {
      coords = "vec3(" + coords + ", float(" + args[3] + "))";
    }
    if (name == "textureSampleCompare" || name == "textureSampleCompareLevel") {
      /* WGSL (t, s, coords, reference); GLSL compares against a vec3/vec4. */
      if (args.size() < 4) {
        fail("textureSampleCompare needs a depth reference argument");
        return "1.0";
      }
      const std::string combined =
          dim == TextureDim::Cube
              ? "vec4(" + args[2] + ", " + args[3] + ")"
              : "vec3(" + args[2] + ", " + args[3] + ")";
      if (name == "textureSampleCompareLevel") {
        return "textureLod(" + pair + ", " + combined + ", 0.0)";
      }
      return "texture(" + pair + ", " + combined + ")";
    }
    if (name == "textureSampleLevel") {
      if (args.size() < 4) {
        fail("textureSampleLevel needs a level argument");
        return "vec4(0.0)";
      }
      return "textureLod(" + pair + ", " + coords + ", " + args[3] + ")";
    }
    if (name == "textureSampleBias") {
      if (args.size() < 4) {
        fail("textureSampleBias needs a bias argument");
        return "vec4(0.0)";
      }
      return "texture(" + pair + ", " + coords + ", " + args[3] + ")";
    }
    if (name == "textureSampleGrad") {
      if (args.size() < 5) {
        fail("textureSampleGrad needs two gradient arguments");
        return "vec4(0.0)";
      }
      return "textureGrad(" + pair + ", " + coords + ", " + args[3] + ", " + args[4] + ")";
    }
    return "texture(" + pair + ", " + coords + ")";
  }

  if (name == "textureLoad") {
    if (node.args.size() < 2) {
      fail("textureLoad needs a texture and coordinates");
      return "vec4(0.0)";
    }
    const std::string texture = ident_of(node.args[0]);
    const GlobalDecl *decl = find_global(texture);
    if (decl != nullptr && decl->type != nullptr && decl->type->kind == Kind::StorageTexture) {
      return "imageLoad(" + global_expr(texture) + ", " + coord_width(
                 decl->type->dim == TextureDim::D3 || decl->type->dim == TextureDim::D2Array ? 3 : 2) +
             "(" + args[1] + "))";
    }
    const int slot = first_pair_for(texture);
    if (slot < 0) {
      fail("textureLoad uses a texture that is not a module-level resource");
      return "vec4(0.0)";
    }
    const TextureDim dim = decl != nullptr && decl->type != nullptr ? decl->type->dim : TextureDim::D2;
    const int width = (dim == TextureDim::D3 || dim == TextureDim::D2Array) ? 3 : 2;
    const std::string level = args.size() >= 3 ? "int(" + args[2] + ")" : "0";
    return "texelFetch(xg_tex" + std::to_string(slot) + ", " + coord_width(width) + "(" + args[1] +
           "), " + level + ")";
  }

  if (name == "textureStore") {
    if (node.args.size() < 3) {
      fail("textureStore needs a texture, coordinates and a value");
      return "";
    }
    const std::string texture = ident_of(node.args[0]);
    const GlobalDecl *decl = find_global(texture);
    const int width = decl != nullptr && decl->type != nullptr &&
                              (decl->type->dim == TextureDim::D3 ||
                               decl->type->dim == TextureDim::D2Array)
                          ? 3
                          : 2;
    return "imageStore(" + global_expr(texture) + ", " + coord_width(width) + "(" + args[1] + "), " +
           args[2] + ");\n";
  }

  if (name == "textureDimensions") {
    const std::string texture = ident_of(node.args[0]);
    const int slot = first_pair_for(texture);
    if (slot < 0) {
      fail("textureDimensions uses a texture that is not a module-level resource");
      return "uvec2(0u)";
    }
    const std::string level = args.size() >= 2 ? "int(" + args[1] + ")" : "0";
    const GlobalDecl *decl = find_global(texture);
    const TextureDim dim = decl != nullptr && decl->type != nullptr ? decl->type->dim : TextureDim::D2;
    const std::string size = "textureSize(xg_tex" + std::to_string(slot) + ", " + level + ")";
    if (dim == TextureDim::D1) return "uint(" + size + ")";
    if (dim == TextureDim::D3) return "uvec3(" + size + ")";
    return "uvec2(" + size + ")";
  }

  if (name == "textureNumLevels") {
    const int slot = first_pair_for(ident_of(node.args[0]));
    if (slot < 0) return "0u";
    return "textureQueryLevels(xg_tex" + std::to_string(slot) + ")";
  }

  if (name == "textureNumLayers") {
    const int slot = first_pair_for(ident_of(node.args[0]));
    if (slot < 0) return "0u";
    return "textureQueryLevels(xg_tex" + std::to_string(slot) + ")";
  }

  if (name == "textureGather") {
    if (node.args.size() < 4) {
      fail("textureGather needs a component, texture, sampler and coordinates");
      return "vec4(0.0)";
    }
    const int slot = pair_slot_for(ident_of(node.args[1]), ident_of(node.args[2]));
    if (slot < 0) return "vec4(0.0)";
    return "textureGather(xg_tex" + std::to_string(slot) + ", " + args[3] + ", " + args[0] + ")";
  }

  /* -- casts, select and the small spelling differences ------------------ */
  if (name == "select") {
    if (node.args.size() < 3) return "0";
    const TypePtr condition = type_of(*node.args[2]);
    if (condition != nullptr && condition->kind == Kind::Vector) {
      return "mix(" + args[0] + ", " + args[1] + ", " + args[2] + ")";
    }
    return "(" + args[2] + " ? " + args[1] + " : " + args[0] + ")";
  }
  if (name == "saturate") return "clamp(" + args[0] + ", 0.0, 1.0)";
  if (name == "atan2") return "atan(" + joined(0) + ")";
  if (name == "inverseSqrt") return "inversesqrt(" + joined(0) + ")";
  if (name == "round") return "roundEven(" + joined(0) + ")";
  if (name == "countOneBits") return "bitCount(" + joined(0) + ")";
  if (name == "reverseBits") return "bitfieldReverse(" + joined(0) + ")";
  if (name == "firstLeadingBit") return "findMSB(" + joined(0) + ")";
  if (name == "firstTrailingBit") return "findLSB(" + joined(0) + ")";
  if (name == "extractBits") return "bitfieldExtract(" + joined(0) + ")";
  if (name == "insertBits") return "bitfieldInsert(" + joined(0) + ")";
  if (name == "dpdx") return "dFdx(" + joined(0) + ")";
  if (name == "dpdy") return "dFdy(" + joined(0) + ")";
  if (name == "fwidth") return "fwidth(" + joined(0) + ")";
  if (name == "workgroupBarrier" || name == "storageBarrier") return "barrier()";
  if (name == "textureBarrier") return "textureBarrier()";
  if (name == "f32" || name == "f16") return "float(" + joined(0) + ")";
  if (name == "i32") return "int(" + joined(0) + ")";
  if (name == "u32") return "uint(" + joined(0) + ")";
  if (name == "bool") return "bool(" + joined(0) + ")";

  if (name == "arrayLength") {
    const Expr &operand = *node.args[0];
    if (operand.kind == Expr::K::Unary && !operand.args.empty() &&
        operand.args[0]->kind == Expr::K::Ident) {
      const GlobalDecl *decl = find_global(operand.args[0]->text);
      if (decl != nullptr && decl->type != nullptr && decl->type->kind == Kind::Array) {
        if (decl->type->length > 0) return std::to_string(decl->type->length);
        return "uint(" + expr(*operand.args[0]) + ".length())";
      }
    }
    return "0u";
  }

  if (name.compare(0, 6, "atomic") == 0) {
    fail("WGSL atomics are not supported by the WebGPU backend");
    return "0u";
  }

  if (is_pass_through_builtin(name)) return name + "(" + joined(0) + ")";

  fail("unsupported WGSL function '" + name + "'");
  return name + "(" + joined(0) + ")";
}

}  // namespace wgsl
}  // namespace xtgui
