/*
 * xbintsc GUI engine — WGSL statement emission.
 */

#include <string>

#include "wgsl_emit.h"

namespace xtgui {
namespace wgsl {
namespace {

std::string spaces(int indent) { return std::string((size_t)indent * 2, ' '); }

std::string zero_value(const Type &type) {
  if (type.kind == Kind::Scalar) {
    switch (type.scalar) {
      case Scalar::Bool: return "false";
      case Scalar::I32: return "0";
      case Scalar::U32: return "0u";
      default: return "0.0";
    }
  }
  if (type.kind == Kind::Vector) {
    return glsl_type(type) + "(" + zero_value(*make_scalar(type.scalar)) + ")";
  }
  return std::string();
}

std::string assignment_text(Emitter *emitter, const Stmt &node) {
  if (node.expr == nullptr) return std::string();
  const std::string left = emitter->expr(*node.expr);
  if (node.op == "++" || node.op == "--") return left + node.op + ";";
  if (node.value == nullptr) return std::string();
  return left + " " + node.op + " " + emitter->expr(*node.value) + ";";
}

}  // namespace

std::string Emitter::statement(const Stmt &node, int indent) {
  const std::string pad = spaces(indent);
  switch (node.kind) {
    case Stmt::K::Block: {
      std::string text = pad + "{\n";
      push_scope();
      for (const StmtPtr &child : node.body) {
        if (child != nullptr) text += statement(*child, indent + 1);
      }
      pop_scope();
      text += pad + "}\n";
      return text;
    }
    case Stmt::K::Var: {
      TypePtr type = node.type;
      if (type == nullptr && node.expr != nullptr) type = type_of(*node.expr);
      if (type == nullptr || type->kind == Kind::Unknown) {
        fail("cannot infer the type of '" + node.name + "'");
        return std::string();
      }
      declare(node.name, type);
      const Local *local = lookup_local(node.name);
      std::string text = pad + glsl_decl(*type, local->name);
      if (node.expr != nullptr) {
        text += " = " + expr(*node.expr);
      } else {
        const std::string zero = zero_value(*type);
        if (!zero.empty()) text += " = " + zero;
      }
      return text + ";\n";
    }
    case Stmt::K::Assign: {
      const std::string text = assignment_text(this, node);
      return text.empty() ? std::string() : pad + text + "\n";
    }
    case Stmt::K::Expr:
      return node.expr == nullptr ? std::string() : pad + expr(*node.expr) + ";\n";
    case Stmt::K::If: {
      std::string text = pad + "if (" + (node.expr != nullptr ? expr(*node.expr) : "false") + ") ";
      if (node.body.empty() || node.body[0] == nullptr) {
        text += "{\n" + pad + "}\n";
      } else {
        text += statement(*node.body[0], indent);
        if (!text.empty() && text.back() == '\n') text.pop_back();
        text += "\n";
      }
      for (const StmtPtr &otherwise : node.otherwise) {
        text += pad + "else ";
        if (otherwise != nullptr) {
          text += statement(*otherwise, indent);
          if (!text.empty() && text.back() == '\n') text.pop_back();
          text += "\n";
        }
      }
      return text;
    }
    case Stmt::K::For: {
      std::string init;
      std::string cond;
      std::string update;
      push_scope();
      if (node.init != nullptr) {
        if (node.init->kind == Stmt::K::Var) {
          init = statement(*node.init, 0);
          while (!init.empty() && (init.back() == '\n' || init.back() == ';')) init.pop_back();
        } else {
          init = assignment_text(this, *node.init);
          if (!init.empty() && init.back() == ';') init.pop_back();
        }
      }
      if (node.cond != nullptr && node.cond->expr != nullptr) cond = expr(*node.cond->expr);
      if (node.update != nullptr) {
        update = assignment_text(this, *node.update);
        if (!update.empty() && update.back() == ';') update.pop_back();
      }
      std::string text = pad + "for (" + init + "; " + cond + "; " + update + ") ";
      continuing_stack_.push_back(std::string());
      if (!node.body.empty() && node.body[0] != nullptr) {
        text += statement(*node.body[0], indent);
        if (!text.empty() && text.back() == '\n') text.pop_back();
        text += "\n";
      }
      continuing_stack_.pop_back();
      pop_scope();
      return text;
    }
    case Stmt::K::While: {
      std::string text = pad + "while (" + (node.expr != nullptr ? expr(*node.expr) : "false") + ") ";
      if (!node.body.empty() && node.body[0] != nullptr) {
        text += statement(*node.body[0], indent);
        if (!text.empty() && text.back() == '\n') text.pop_back();
        text += "\n";
      }
      return text;
    }
    case Stmt::K::Loop: {
      std::string continuing;
      if (node.continuing != nullptr) continuing = statement(*node.continuing, indent + 1);
      continuing_stack_.push_back(continuing);
      std::string body;
      for (const StmtPtr &child : node.body) {
        if (child != nullptr) body += statement(*child, indent + 1);
      }
      continuing_stack_.pop_back();
      std::string text = pad + "for (;;) {\n" + body;
      if (!continuing.empty()) text += continuing;
      text += pad + "}\n";
      return text;
    }
    case Stmt::K::Break: return pad + "break;\n";
    case Stmt::K::Continue: {
      std::string text;
      if (!continuing_stack_.empty()) text = continuing_stack_.back();
      return text + pad + "continue;\n";
    }
    case Stmt::K::Return:
      if (node.expr == nullptr) return pad + "return;\n";
      return pad + "return " + expr(*node.expr) + ";\n";
    case Stmt::K::Discard: return pad + "discard;\n";
    case Stmt::K::Switch: {
      std::string text = pad + "switch (" + (node.expr != nullptr ? expr(*node.expr) : "0") + ") {\n";
      for (size_t i = 0; i < node.cases.size(); i++) {
        if (node.cases[i] == nullptr) {
          text += pad + "  default: {\n";
        } else {
          text += pad + "  case " + expr(*node.cases[i]) + ": {\n";
        }
        if (i < node.case_bodies.size() && node.case_bodies[i] != nullptr) {
          if (node.case_bodies[i]->kind == Stmt::K::Block) {
            for (const StmtPtr &child : node.case_bodies[i]->body) {
              if (child != nullptr) text += statement(*child, indent + 2);
            }
          } else {
            text += statement(*node.case_bodies[i], indent + 2);
          }
        }
        text += pad + "    break;\n";
        text += pad + "  }\n";
      }
      return text + pad + "}\n";
    }
    default: break;
  }
  return std::string();
}

std::string Emitter::emit_user_function(const FuncDecl &func) {
  push_scope();
  std::string params;
  for (size_t i = 0; i < func.params.size(); i++) {
    const Param &param = func.params[i];
    if (i > 0) params += ", ";
    declare(param.name, param.type);
    const Local *local = lookup_local(param.name);
    params += glsl_decl(*param.type, local->name);
  }
  Type void_type;
  void_type.kind = Kind::Void;
  std::string text = glsl_type(func.ret != nullptr ? *func.ret : void_type) + " xg_" + func.name +
                     "(" + params + ") {\n";
  for (const StmtPtr &statement_node : func.body) {
    if (statement_node != nullptr) text += statement(*statement_node, 1);
  }
  text += "}\n";
  pop_scope();
  return text;
}

}  // namespace wgsl
}  // namespace xtgui
