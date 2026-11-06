/*
 * xbintsc GUI engine — WGSL expression emission.
 *
 * WGSL and GLSL agree on most arithmetic, so the emitter only rewrites where
 * the spelling or the operand order genuinely differs: vector comparisons,
 * float `%`, texture sampling (SDL_GPU binds a texture and a sampler as one
 * pair, so the pair's combined sampler replaces both arguments), array
 * constructors and bitcasts.
 */

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "wgsl_emit.h"

namespace xtgui {
namespace wgsl {
namespace {

std::string float_literal(const std::string &text) {
  std::string value = text;
  if (!value.empty()) {
    const char last = value.back();
    if (last == 'f' || last == 'F' || last == 'h' || last == 'H') value.pop_back();
  }
  if (value.find('.') == std::string::npos && value.find('e') == std::string::npos &&
      value.find('E') == std::string::npos) {
    value += ".0";
  }
  return value;
}

std::string int_literal(const std::string &text) {
  if (!text.empty() && (text.back() == 'i' || text.back() == 'I')) return text.substr(0, text.size() - 1);
  return text;
}

std::string zero_for(Scalar scalar) {
  switch (scalar) {
    case Scalar::Bool: return "false";
    case Scalar::I32: return "0";
    case Scalar::U32: return "0u";
    default: return "0.0";
  }
}

const char *comparison_function(const std::string &op) {
  if (op == "<") return "lessThan";
  if (op == "<=") return "lessThanEqual";
  if (op == ">") return "greaterThan";
  if (op == ">=") return "greaterThanEqual";
  return nullptr;
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

std::string ident_of(const ExprPtr &node) {
  return node != nullptr && node->kind == Expr::K::Ident ? node->text : std::string();
}

}  // namespace

std::string Emitter::cast_to(const Type &target, const std::string &value) const {
  return glsl_type(target) + "(" + value + ")";
}

std::string Emitter::expr(const Expr &node) {
  switch (node.kind) {
    case Expr::K::Int: return int_literal(node.text);
    case Expr::K::Float: return float_literal(node.text);
    case Expr::K::Bool: return node.text;
    case Expr::K::Ident: {
      if (const Local *local = lookup_local(node.text)) return local->name;
      if (const GlobalDecl *global = find_global(node.text)) {
        if (global->type != nullptr && global->type->kind == Kind::Sampler) return "";
        if (global->type != nullptr && global->type->kind == Kind::Texture) {
          return global_expr(node.text);
        }
        return global_expr(node.text);
      }
      return "xg_" + node.text;
    }
    case Expr::K::Unary: {
      if (node.args.empty()) return "0";
      if (node.text == "&" || node.text == "*") {
        fail("WGSL pointers are not supported by the WebGPU backend");
        return expr(*node.args[0]);
      }
      return "(" + node.text + expr(*node.args[0]) + ")";
    }
    case Expr::K::Binary: {
      if (node.args.size() < 2) return "0";
      const std::string left = expr(*node.args[0]);
      const std::string right = expr(*node.args[1]);
      const TypePtr left_type = type_of(*node.args[0]);
      const TypePtr right_type = type_of(*node.args[1]);
      const int width = left_type != nullptr && left_type->width > 1
                            ? left_type->width
                            : (right_type != nullptr ? right_type->width : 1);
      if (width > 1 && (node.text == "<" || node.text == "<=" || node.text == ">" ||
                        node.text == ">=")) {
        return std::string(comparison_function(node.text)) + "(" + left + ", " + right + ")";
      }
      if (node.text == "%" && left_type != nullptr && left_type->is_numeric()) {
        /* GLSL `%` is integer-only; WGSL defines float remainder as `mod`. */
        if (left_type->scalar == Scalar::F32) return "mod(" + left + ", " + right + ")";
        return "(" + left + " % " + right + ")";
      }
      return "(" + left + " " + node.text + " " + right + ")";
    }
    case Expr::K::Member: {
      if (node.args.empty()) return "0";
      const std::string base = expr(*node.args[0]);
      const TypePtr base_type = type_of(*node.args[0]);
      if (base_type != nullptr && base_type->kind == Kind::Struct) {
        return base + "." + member_name(node.member);
      }
      if (node.member == "length") {
        /* A fixed-size array's length is known statically. */
        if (base_type != nullptr && base_type->kind == Kind::Array) {
          return std::to_string(base_type->length);
        }
        return "uint(" + base + ".length())";
      }
      return base + "." + node.member;
    }
    case Expr::K::Index: {
      if (node.args.size() < 2) return "0";
      return expr(*node.args[0]) + "[" + expr(*node.args[1]) + "]";
    }
    case Expr::K::ArrayCtor: {
      if (node.type == nullptr || node.type->elem == nullptr) return "0";
      const std::string type = glsl_type(*node.type);
      std::string body;
      for (size_t i = 0; i < node.args.size(); i++) {
        if (i > 0) body += ", ";
        body += expr(*node.args[i]);
      }
      if (node.args.empty()) {
        if (node.type->elem->kind == Kind::Struct) {
          fail("a zero-initialised array of structs is not supported; list its elements");
          return type;
        }
        for (int i = 0; i < node.type->length; i++) {
          if (i > 0) body += ", ";
          body += zero_for(node.type->elem->scalar);
        }
      }
      return type + "(" + body + ")";
    }
    case Expr::K::Bitcast: {
      const TypePtr source = node.args.empty() ? nullptr : type_of(*node.args[0]);
      const TypePtr target = node.type;
      if (source == nullptr || target == nullptr) return "0";
      const std::string value = node.args.empty() ? "0" : expr(*node.args[0]);
      const bool source_float = source->scalar == Scalar::F32;
      const bool target_float = target->scalar == Scalar::F32;
      if (source_float && !target_float) {
        return (target->scalar == Scalar::U32 ? "floatBitsToUint" : "floatBitsToInt") + std::string("(") +
               value + ")";
      }
      if (!source_float && target_float) {
        return (source->scalar == Scalar::U32 ? "uintBitsToFloat" : "intBitsToFloat") + std::string("(") +
               value + ")";
      }
      return cast_to(*target, value);
    }
    case Expr::K::Call: return call_expr(node);
    default: break;
  }
  return "0";
}

}  // namespace wgsl
}  // namespace xtgui
