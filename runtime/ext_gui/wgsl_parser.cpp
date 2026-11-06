/*
 * xbintsc GUI engine — WGSL top-level declarations, types and attributes.
 *
 * Statements live in `wgsl_parse_stmt.cpp`, expressions in
 * `wgsl_parse_expr.cpp`; this unit owns the module grammar that ties them
 * together.
 */

#include "wgsl_parser.h"

#include <cstdlib>

namespace xtgui {
namespace wgsl {

/* -- token helpers -------------------------------------------------------- */

const Token &Parser::token(size_t index) const {
  if (index >= tokens_.size()) return tokens_.back();
  return tokens_[index];
}

const Token &Parser::peek(int ahead) const {
  const int index = (int)position_ + ahead;
  if (index < 0) return tokens_.front();
  if ((size_t)index >= tokens_.size()) return tokens_.back();
  return tokens_[(size_t)index];
}

bool Parser::at(const std::string &text) const { return peek().text == text; }

bool Parser::accept(const std::string &text) {
  if (!at(text)) return false;
  position_++;
  return true;
}

bool Parser::expect(const std::string &text) {
  if (accept(text)) return true;
  fail("expected '" + text + "'");
  return false;
}

bool Parser::expect_ident(std::string *out) {
  if (peek().kind != Tok::Ident) {
    fail("expected an identifier");
    return false;
  }
  *out = peek().text;
  position_++;
  return true;
}

void Parser::fail(const std::string &message) { fail_at(peek(), message); }

void Parser::fail_at(const Token &token, const std::string &message) {
  if (failed_) return;
  failed_ = true;
  error_line_ = token.line;
  error_column_ = token.column;
  error_ = std::to_string(token.line) + ":" + std::to_string(token.column) + ": " + message;
}

/* -- module --------------------------------------------------------------- */

bool Parser::parse_module() {
  while (peek().kind != Tok::End && !failed_) {
    std::vector<Attribute> attrs;
    if (!parse_attrs(&attrs)) return false;
    if (peek().kind == Tok::End) break;
    if (at("enable") || at("requires") || at("diagnostic")) {
      while (peek().kind != Tok::End && !at(";")) position_++;
      if (!expect(";")) return false;
      continue;
    }
    if (at("struct")) {
      if (!parse_struct()) return false;
      continue;
    }
    if (at("alias")) {
      if (!parse_alias()) return false;
      continue;
    }
    if (at("const") || at("override")) {
      if (!parse_const_global()) return false;
      continue;
    }
    if (at("var")) {
      if (!parse_global(attrs)) return false;
      continue;
    }
    if (at("fn")) {
      if (!parse_function(attrs)) return false;
      continue;
    }
    if (accept(";")) continue;
    fail("expected a WGSL declaration (struct, alias, const, var or fn)");
    return false;
  }
  return !failed_;
}

bool Parser::parse_struct() {
  if (!expect("struct")) return false;
  StructDecl decl;
  if (!expect_ident(&decl.name)) return false;
  if (!expect("{")) return false;
  while (!at("}") && !failed_) {
    std::vector<Attribute> attrs;
    if (!parse_attrs(&attrs)) return false;
    Member member;
    if (!expect_ident(&member.name)) return false;
    if (!expect(":")) return false;
    member.type = parse_type();
    if (member.type == nullptr) return false;
    for (const Attribute &attr : attrs) {
      if (attr.name == "location") member.location = attr.ints.empty() ? 0 : attr.ints[0];
      else if (attr.name == "builtin" && !attr.words.empty()) member.builtin = attr.words[0];
      else if (attr.name == "interpolate" && !attr.words.empty()) member.interpolate = attr.words[0];
      else if (attr.name == "align" && !attr.ints.empty()) member.align = attr.ints[0];
      else if (attr.name == "size" && !attr.ints.empty()) member.size = attr.ints[0];
    }
    decl.members.push_back(std::move(member));
    if (!accept(",")) break;
  }
  if (!expect("}")) return false;
  accept(";");
  module_->structs.push_back(std::move(decl));
  return true;
}

bool Parser::parse_alias() {
  if (!expect("alias")) return false;
  std::string name;
  if (!expect_ident(&name)) return false;
  if (!expect("=")) return false;
  TypePtr type = parse_type();
  if (type == nullptr) return false;
  if (!expect(";")) return false;
  module_->aliases.emplace_back(name, type);
  return true;
}

bool Parser::parse_const_global() {
  const bool is_override = at("override");
  position_++;
  GlobalDecl decl;
  if (!expect_ident(&decl.name)) return false;
  if (accept(":")) {
    decl.type = parse_type();
    if (decl.type == nullptr) return false;
  }
  if (accept("=")) {
    decl.init = parse_expr();
    if (decl.init == nullptr) return false;
  }
  (void)is_override;
  if (!expect(";")) return false;
  decl.space = Space::None;
  module_->globals.push_back(std::move(decl));
  return true;
}

bool Parser::parse_global(const std::vector<Attribute> &attrs) {
  if (!expect("var")) return false;
  GlobalDecl decl;
  if (accept("<")) {
    std::string space;
    if (!expect_ident(&space)) return false;
    if (space == "private") decl.space = Space::Private;
    else if (space == "workgroup") decl.space = Space::Workgroup;
    else if (space == "uniform") decl.space = Space::Uniform;
    else if (space == "storage") decl.space = Space::Storage;
    else {
      fail("unsupported address space '" + space + "'");
      return false;
    }
    if (accept(",")) {
      if (!expect_ident(&decl.access)) return false;
    }
    if (!expect(">")) return false;
  }
  if (!expect_ident(&decl.name)) return false;
  if (!expect(":")) return false;
  decl.type = parse_type();
  if (decl.type == nullptr) return false;
  if (accept("=")) {
    decl.init = parse_expr();
    if (decl.init == nullptr) return false;
  }
  if (!expect(";")) return false;
  for (const Attribute &attr : attrs) {
    if (attr.name == "group") decl.group = attr.ints.empty() ? 0 : attr.ints[0];
    else if (attr.name == "binding") decl.binding = attr.ints.empty() ? 0 : attr.ints[0];
  }
  decl.line = peek().line;
  module_->globals.push_back(std::move(decl));
  return true;
}

bool Parser::parse_workgroup_size(const Attribute &attr, int out[3]) {
  out[0] = attr.ints.size() > 0 ? attr.ints[0] : 1;
  out[1] = attr.ints.size() > 1 ? attr.ints[1] : 1;
  out[2] = attr.ints.size() > 2 ? attr.ints[2] : 1;
  return out[0] > 0 && out[1] > 0 && out[2] > 0;
}

bool Parser::parse_function(const std::vector<Attribute> &attrs) {
  if (!expect("fn")) return false;
  FuncDecl decl;
  if (!expect_ident(&decl.name)) return false;
  if (!expect("(")) return false;
  while (!at(")") && !failed_) {
    std::vector<Attribute> param_attrs;
    if (!parse_attrs(&param_attrs)) return false;
    Param param;
    if (!expect_ident(&param.name)) return false;
    if (!expect(":")) return false;
    param.type = parse_type();
    if (param.type == nullptr) return false;
    for (const Attribute &attr : param_attrs) {
      if (attr.name == "location") param.location = attr.ints.empty() ? 0 : attr.ints[0];
      else if (attr.name == "builtin" && !attr.words.empty()) param.builtin = attr.words[0];
      else if (attr.name == "interpolate" && !attr.words.empty()) param.interpolate = attr.words[0];
    }
    decl.params.push_back(std::move(param));
    if (!accept(",")) break;
  }
  if (!expect(")")) return false;
  if (accept("->")) {
    std::vector<Attribute> ret_attrs;
    if (!parse_attrs(&ret_attrs)) return false;
    decl.ret = parse_type();
    if (decl.ret == nullptr) return false;
    for (const Attribute &attr : ret_attrs) {
      if (attr.name == "location") decl.ret_location = attr.ints.empty() ? 0 : attr.ints[0];
      else if (attr.name == "builtin" && !attr.words.empty()) decl.ret_builtin = attr.words[0];
      else if (attr.name == "interpolate" && !attr.words.empty()) {
        decl.ret_interpolate = attr.words[0];
      }
    }
  }
  for (const Attribute &attr : attrs) {
    if (attr.name == "vertex") {
      decl.is_entry = true;
      decl.stage = "vertex";
    } else if (attr.name == "fragment") {
      decl.is_entry = true;
      decl.stage = "fragment";
    } else if (attr.name == "compute") {
      decl.is_entry = true;
      decl.stage = "compute";
    } else if (attr.name == "workgroup_size") {
      decl.has_workgroup = parse_workgroup_size(attr, decl.workgroup);
    }
  }
  if (decl.is_entry && decl.stage == "compute" && !decl.has_workgroup) {
    fail("a @compute entry point needs @workgroup_size");
    return false;
  }
  if (!expect("{")) return false;
  while (!at("}") && !failed_) {
    StmtPtr statement = parse_stmt();
    if (statement == nullptr) return false;
    decl.body.push_back(std::move(statement));
  }
  if (!expect("}")) return false;
  module_->funcs.push_back(std::move(decl));
  return true;
}

/* -- attributes ----------------------------------------------------------- */

bool Parser::parse_attrs(std::vector<Attribute> *attrs) {
  while (peek().kind == Tok::Ident && !peek().text.empty() && peek().text[0] == '@') {
    Attribute attr;
    attr.name = peek().text.substr(1);
    position_++;
    if (accept("(")) {
      while (!at(")") && !failed_) {
        if (peek().kind == Tok::Int) {
          attr.ints.push_back((int)strtol(peek().text.c_str(), nullptr, 0));
          attr.words.push_back(peek().text);
          position_++;
        } else if (peek().kind == Tok::Ident) {
          attr.words.push_back(peek().text);
          position_++;
        } else {
          fail("expected an attribute argument");
          return false;
        }
        if (!accept(",")) break;
      }
      if (!expect(")")) return false;
    }
    attrs->push_back(std::move(attr));
  }
  return true;
}

/* -- types ---------------------------------------------------------------- */

bool Parser::expect_close_angle() {
  if (accept(">")) return true;
  if (at(">>")) {
    /* Close one level now and remember the second for the outer type. */
    position_++;
    pending_angles_++;
    return true;
  }
  fail("expected '>'");
  return false;
}

TypePtr Parser::parse_texture_type(const std::string &name) {
  auto type = std::make_shared<Type>();
  type->kind = Kind::Texture;
  if (name.find("storage") != std::string::npos) type->kind = Kind::StorageTexture;
  if (name.find("depth") != std::string::npos) type->depth = true;
  if (name.find("_1d") != std::string::npos) type->dim = TextureDim::D1;
  else if (name.find("_2d_array") != std::string::npos) type->dim = TextureDim::D2Array;
  else if (name.find("_3d") != std::string::npos) type->dim = TextureDim::D3;
  else if (name.find("cube_array") != std::string::npos) type->dim = TextureDim::CubeArray;
  else if (name.find("cube") != std::string::npos) type->dim = TextureDim::Cube;
  else if (name.find("multisampled") != std::string::npos) type->dim = TextureDim::Multisampled2D;
  else type->dim = TextureDim::D2;

  if (!accept("<")) return type;
  if (type->kind == Kind::StorageTexture) {
    std::string format;
    if (!expect_ident(&format)) return nullptr;
    if (format == "rgba8unorm") type->format = ImageFormat::Rgba8Unorm;
    else if (format == "rgba8snorm") type->format = ImageFormat::Rgba8Snorm;
    else if (format == "rgba8uint") type->format = ImageFormat::Rgba8Uint;
    else if (format == "rgba8sint") type->format = ImageFormat::Rgba8Sint;
    else if (format == "rgba16float") type->format = ImageFormat::Rgba16Float;
    else if (format == "rgba32float") type->format = ImageFormat::Rgba32Float;
    else if (format == "r32float") type->format = ImageFormat::R32Float;
    else if (format == "r32uint") type->format = ImageFormat::R32Uint;
    else if (format == "r32sint") type->format = ImageFormat::R32Sint;
    else if (format == "rgba16uint") type->format = ImageFormat::Rgba16Uint;
    else if (format == "rgba16sint") type->format = ImageFormat::Rgba16Sint;
    else if (format == "bgra8unorm") type->format = ImageFormat::Bgra8Unorm;
    else {
      fail("unsupported storage texture format '" + format + "'");
      return nullptr;
    }
    if (accept(",")) {
      std::string access;
      if (!expect_ident(&access)) return nullptr;
      if (access == "read") type->access = 1;
      else if (access == "write") type->access = 2;
      else if (access == "read_write") type->access = 3;
    } else {
      type->access = 2;
    }
  } else if (!type->depth) {
    TypePtr element = parse_type();
    if (element == nullptr) return nullptr;
    if (element->kind != Kind::Scalar) {
      fail("a sampled texture's element type must be f32, i32 or u32");
      return nullptr;
    }
    type->scalar = element->scalar;
  }
  if (!expect_close_angle()) return nullptr;
  return type;
}

TypePtr Parser::parse_type() {
  if (peek().kind != Tok::Ident) {
    fail("expected a type");
    return nullptr;
  }
  const std::string name = peek().text;
  position_++;
  if (name.compare(0, 7, "texture") == 0) return parse_texture_type(name);
  if (name == "ptr") {
    if (!expect("<")) return nullptr;
    std::string space;
    if (!expect_ident(&space)) return nullptr;
    if (!expect(",")) return nullptr;
    TypePtr inner = parse_type();
    if (inner == nullptr) return nullptr;
    if (!expect_close_angle()) return nullptr;
    auto type = std::make_shared<Type>();
    type->kind = Kind::Ptr;
    type->elem = inner;
    return type;
  }
  if (name == "array") {
    if (!expect("<")) return nullptr;
    TypePtr elem = parse_type();
    if (elem == nullptr) return nullptr;
    int length = 0;
    if (accept(",")) {
      if (peek().kind != Tok::Int) {
        fail("an array length must be an integer literal");
        return nullptr;
      }
      length = (int)strtol(peek().text.c_str(), nullptr, 0);
      position_++;
    }
    if (!expect_close_angle()) return nullptr;
    return make_array(elem, length);
  }
  if (accept("<")) {
    Scalar scalar = Scalar::F32;
    if (peek().kind == Tok::Ident) {
      const std::string arg = peek().text;
      if (arg == "f32" || arg == "f16") scalar = Scalar::F32;
      else if (arg == "i32") scalar = Scalar::I32;
      else if (arg == "u32") scalar = Scalar::U32;
      else if (arg == "bool") scalar = Scalar::Bool;
      else {
        fail("unsupported type argument '" + arg + "'");
        return nullptr;
      }
      position_++;
    } else {
      fail("expected a scalar type argument");
      return nullptr;
    }
    if (!expect_close_angle()) return nullptr;
    if (name.compare(0, 3, "vec") == 0 && name.size() == 4) {
      return make_vector(scalar, name[3] - '0');
    }
    if (name.compare(0, 3, "mat") == 0) {
      int cols = 4;
      int rows = 4;
      if (name.size() >= 5 && name[4] == 'x') {
        cols = name[3] - '0';
        rows = name[5] - '0';
      } else if (name.size() == 4) {
        cols = rows = name[3] - '0';
      }
      return make_matrix(scalar, cols, rows);
    }
    if (name == "atomic") {
      auto type = std::make_shared<Type>();
      type->kind = Kind::Atomic;
      type->scalar = scalar;
      return type;
    }
    fail("type '" + name + "' does not take type arguments");
    return nullptr;
  }
  TypePtr shorthand = type_by_name(*module_, name);
  if (shorthand == nullptr) {
    fail("unknown type '" + name + "'");
    return nullptr;
  }
  return shorthand;
}

}  // namespace wgsl
}  // namespace xtgui
