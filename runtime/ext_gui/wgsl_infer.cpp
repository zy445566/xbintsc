/*
 * xbintsc GUI engine — WGSL expression typing.
 *
 * GLSL needs a declared type for every `let` and differs from WGSL where an
 * operator's spelling is not enough (vector comparisons become `lessThan`, a
 * float `%` becomes `mod`, …), so the emitter asks this pass for the type of
 * the expressions it writes.
 */

#include "wgsl_emit.h"

namespace xtgui {
namespace wgsl {
namespace {

TypePtr unknown() {
  auto type = std::make_shared<Type>();
  type->kind = Kind::Unknown;
  return type;
}

TypePtr vector_of(Scalar scalar, int width) {
  return width > 1 ? make_vector(scalar, width) : make_scalar(scalar);
}

int width_of(const TypePtr &type) {
  if (type == nullptr) return 1;
  if (type->kind == Kind::Vector) return type->width;
  return 1;
}

TypePtr common(const TypePtr &left, const TypePtr &right) {
  if (left == nullptr || left->kind == Kind::Unknown) return right;
  if (right == nullptr || right->kind == Kind::Unknown) return left;
  if (!left->is_numeric() || !right->is_numeric()) return left;
  Scalar scalar = left->scalar;
  if (left->scalar == Scalar::F32 || right->scalar == Scalar::F32) scalar = Scalar::F32;
  else if (left->scalar == Scalar::U32 || right->scalar == Scalar::U32) scalar = Scalar::U32;
  const int width = width_of(left) > width_of(right) ? width_of(left) : width_of(right);
  return vector_of(scalar, width);
}

TypePtr scalar_type(Scalar scalar) { return make_scalar(scalar); }

TypePtr texture_result(const std::string &name, const std::vector<TypePtr> &args) {
  if (name.compare(0, 15, "textureSample") == 0) {
    if (name.find("Compare") != std::string::npos) return scalar_type(Scalar::F32);
    return make_vector(Scalar::F32, 4);
  }
  if (name == "textureLoad") return make_vector(Scalar::F32, 4);
  if (name == "textureGather") return make_vector(Scalar::F32, 4);
  if (name == "textureDimensions") {
    if (name == "textureDimensions") return make_vector(Scalar::U32, 2);
  }
  (void)args;
  return nullptr;
}

TypePtr builtin_result(const std::string &name, const std::vector<TypePtr> &args) {
  const TypePtr first = args.empty() ? nullptr : args[0];
  if (name == "f32" || name == "f16") return scalar_type(Scalar::F32);
  if (name == "i32") return scalar_type(Scalar::I32);
  if (name == "u32") return scalar_type(Scalar::U32);
  if (name == "bool") return scalar_type(Scalar::Bool);
  if (name == "dot" || name == "length" || name == "distance") return scalar_type(Scalar::F32);
  if (name == "determinant") return scalar_type(Scalar::F32);
  if (name == "all" || name == "any") return scalar_type(Scalar::Bool);
  if (name == "select") return first;
  if (name == "arrayLength") return scalar_type(Scalar::U32);
  if (name == "cross") return make_vector(Scalar::F32, 3);
  if (name == "transpose") {
    if (first != nullptr && first->kind == Kind::Matrix) {
      return make_matrix(first->scalar, first->rows, first->cols);
    }
    return first;
  }
  if (name == "countOneBits" || name == "reverseBits" || name == "firstLeadingBit" ||
      name == "firstTrailingBit" || name == "extractBits" || name == "insertBits") {
    return first;
  }
  if (name.size() > 5 && name.compare(0, 7, "texture") == 0) {
    TypePtr result = texture_result(name, args);
    if (result != nullptr) return result;
  }
  return first;
}

}  // namespace

TypePtr Emitter::type_of(const Expr &node) {
  switch (node.kind) {
    case Expr::K::Int: {
      if (!node.text.empty() && node.text.back() == 'u') return scalar_type(Scalar::U32);
      return scalar_type(Scalar::I32);
    }
    case Expr::K::Float: return scalar_type(Scalar::F32);
    case Expr::K::Bool: return scalar_type(Scalar::Bool);
    case Expr::K::Ident: {
      if (const Local *local = lookup_local(node.text)) return local->type;
      if (const GlobalDecl *global = find_global(node.text)) return global->type;
      return unknown();
    }
    case Expr::K::Unary: {
      TypePtr operand = node.args.empty() ? nullptr : type_of(*node.args[0]);
      if (node.text == "!") {
        if (operand != nullptr && operand->kind == Kind::Vector) {
          return make_vector(Scalar::Bool, operand->width);
        }
        return scalar_type(Scalar::Bool);
      }
      return operand;
    }
    case Expr::K::Binary: {
      TypePtr left = node.args.size() > 0 ? type_of(*node.args[0]) : nullptr;
      TypePtr right = node.args.size() > 1 ? type_of(*node.args[1]) : nullptr;
      if (node.text == "&&" || node.text == "||") return scalar_type(Scalar::Bool);
      if (node.text == "==" || node.text == "!=") return scalar_type(Scalar::Bool);
      if (node.text == "<" || node.text == ">" || node.text == "<=" || node.text == ">=") {
        const int width = width_of(left) > width_of(right) ? width_of(left) : width_of(right);
        return vector_of(Scalar::Bool, width);
      }
      if (node.text == "<<" || node.text == ">>") return left;
      return common(left, right);
    }
    case Expr::K::Index: {
      TypePtr base = node.args.empty() ? nullptr : type_of(*node.args[0]);
      if (base == nullptr) return unknown();
      if (base->kind == Kind::Array) return base->elem;
      if (base->kind == Kind::Vector) return scalar_type(base->scalar);
      if (base->kind == Kind::Matrix) return make_vector(base->scalar, base->rows);
      return unknown();
    }
    case Expr::K::Member: {
      TypePtr base = node.args.empty() ? nullptr : type_of(*node.args[0]);
      if (base == nullptr) return unknown();
      if (base->kind == Kind::Struct && base->strct != nullptr) {
        for (const Member &member : base->strct->members) {
          if (member.name == node.member) return member.type;
        }
        return unknown();
      }
      if (base->kind == Kind::Vector) {
        if (node.member == "length") return scalar_type(Scalar::U32);
        const int width = (int)node.member.size();
        return vector_of(base->scalar, width);
      }
      return unknown();
    }
    case Expr::K::ArrayCtor: return node.type;
    case Expr::K::Bitcast: return node.type;
    case Expr::K::Call: {
      if (!node.text.empty() && node.text[0] == '.') {
        if (node.text == ".length") return scalar_type(Scalar::U32);
        return unknown();
      }
      std::vector<TypePtr> args;
      args.reserve(node.args.size());
      for (const ExprPtr &arg : node.args) {
        if (arg != nullptr) args.push_back(type_of(*arg));
      }
      if (TypePtr constructed = type_by_name(module_, node.text)) return constructed;
      if (const FuncDecl *function = find_function(node.text)) return function->ret;
      return builtin_result(node.text, args);
    }
    default: break;
  }
  return unknown();
}

}  // namespace wgsl
}  // namespace xtgui
