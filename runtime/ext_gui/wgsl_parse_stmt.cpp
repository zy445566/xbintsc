/*
 * xbintsc GUI engine — WGSL statements.
 */

#include "wgsl_parser.h"

namespace xtgui {
namespace wgsl {
namespace {

bool is_assignment(const std::string &text) {
  return text == "=" || text == "+=" || text == "-=" || text == "*=" || text == "/=" ||
         text == "%=" || text == "&=" || text == "|=" || text == "^=" || text == "<<=" ||
         text == ">>=";
}

StmtPtr make_stmt(Stmt::K kind, int line) {
  auto statement = std::make_unique<Stmt>();
  statement->kind = kind;
  statement->line = line;
  return statement;
}

}  // namespace

StmtPtr Parser::parse_block() {
  if (!expect("{")) return nullptr;
  auto block = make_stmt(Stmt::K::Block, peek().line);
  while (!at("}") && !failed_) {
    StmtPtr statement = parse_stmt();
    if (statement == nullptr) return nullptr;
    block->body.push_back(std::move(statement));
  }
  if (!expect("}")) return nullptr;
  return block;
}

StmtPtr Parser::parse_simple_stmt() {
  ExprPtr expr = parse_expr();
  if (expr == nullptr) return nullptr;
  if (peek().kind == Tok::Punct && is_assignment(peek().text)) {
    auto statement = make_stmt(Stmt::K::Assign, peek().line);
    statement->op = peek().text;
    position_++;
    statement->expr = std::move(expr);
    statement->value = parse_expr();
    if (statement->value == nullptr) return nullptr;
    return statement;
  }
  auto statement = make_stmt(Stmt::K::Expr, expr->line);
  statement->expr = std::move(expr);
  return statement;
}

StmtPtr Parser::parse_stmt() {
  const int line = peek().line;
  if (at("{")) return parse_block();

  if (at("if")) {
    position_++;
    auto statement = make_stmt(Stmt::K::If, line);
    if (!expect("(")) return nullptr;
    statement->expr = parse_expr();
    if (statement->expr == nullptr) return nullptr;
    if (!expect(")")) return nullptr;
    statement->body.push_back(parse_block());
    if (statement->body.back() == nullptr) return nullptr;
    if (accept("else")) {
      if (at("if")) {
        statement->otherwise.push_back(parse_stmt());
      } else {
        statement->otherwise.push_back(parse_block());
      }
      if (statement->otherwise.back() == nullptr) return nullptr;
    }
    return statement;
  }

  if (at("for")) {
    position_++;
    auto statement = make_stmt(Stmt::K::For, line);
    if (!expect("(")) return nullptr;
    if (at("var") || at("let") || at("const")) {
      statement->init = parse_stmt();
      if (statement->init == nullptr) {
        /* `parse_stmt` consumed the terminator; rewind is impossible, so the
         * local-declaration path below parses without it instead. */
        return nullptr;
      }
    } else if (!at(";")) {
      statement->init = parse_simple_stmt();
      if (statement->init == nullptr) return nullptr;
      if (!expect(";")) return nullptr;
    } else {
      if (!expect(";")) return nullptr;
    }
    if (!at(";")) {
      statement->cond = make_stmt(Stmt::K::Expr, peek().line);
      statement->cond->expr = parse_expr();
      if (statement->cond->expr == nullptr) return nullptr;
    }
    if (!expect(";")) return nullptr;
    if (!at(")")) {
      statement->update = parse_simple_stmt();
      if (statement->update == nullptr) return nullptr;
    }
    if (!expect(")")) return nullptr;
    statement->body.push_back(parse_block());
    if (statement->body.back() == nullptr) return nullptr;
    return statement;
  }

  if (at("while")) {
    position_++;
    auto statement = make_stmt(Stmt::K::While, line);
    if (!expect("(")) return nullptr;
    statement->expr = parse_expr();
    if (statement->expr == nullptr) return nullptr;
    if (!expect(")")) return nullptr;
    statement->body.push_back(parse_block());
    if (statement->body.back() == nullptr) return nullptr;
    return statement;
  }

  if (at("loop")) {
    position_++;
    auto statement = make_stmt(Stmt::K::Loop, line);
    if (!expect("{")) return nullptr;
    while (!at("}") && !at("continuing") && !failed_) {
      StmtPtr child = parse_stmt();
      if (child == nullptr) return nullptr;
      statement->body.push_back(std::move(child));
    }
    if (accept("continuing")) {
      statement->continuing = parse_block();
      if (statement->continuing == nullptr) return nullptr;
    }
    if (!expect("}")) return nullptr;
    return statement;
  }

  if (at("switch")) {
    position_++;
    auto statement = make_stmt(Stmt::K::Switch, line);
    if (!expect("(")) return nullptr;
    statement->expr = parse_expr();
    if (statement->expr == nullptr) return nullptr;
    if (!expect(")")) return nullptr;
    if (!expect("{")) return nullptr;
    while (!at("}") && !failed_) {
      ExprPtr selector;
      if (accept("default")) {
        selector = nullptr;
      } else {
        if (!expect("case")) return nullptr;
        selector = parse_expr();
        if (selector == nullptr) return nullptr;
        while (accept(",")) {
          ExprPtr next = parse_expr();
          if (next == nullptr) return nullptr;
        }
      }
      if (!expect(":")) return nullptr;
      StmtPtr body = at("{") ? parse_block() : parse_stmt();
      if (body == nullptr) return nullptr;
      statement->cases.push_back(std::move(selector));
      statement->case_bodies.push_back(std::move(body));
      accept(",");
    }
    if (!expect("}")) return nullptr;
    return statement;
  }

  if (at("break")) {
    position_++;
    if (accept("if")) {
      auto statement = make_stmt(Stmt::K::If, line);
      if (!expect("(")) return nullptr;
      statement->expr = parse_expr();
      if (statement->expr == nullptr) return nullptr;
      if (!expect(")")) return nullptr;
      if (!expect(";")) return nullptr;
      statement->body.push_back(make_stmt(Stmt::K::Break, line));
      return statement;
    }
    if (!expect(";")) return nullptr;
    return make_stmt(Stmt::K::Break, line);
  }

  if (at("continue")) {
    position_++;
    if (!expect(";")) return nullptr;
    return make_stmt(Stmt::K::Continue, line);
  }

  if (at("discard")) {
    position_++;
    if (!expect(";")) return nullptr;
    return make_stmt(Stmt::K::Discard, line);
  }

  if (at("return")) {
    position_++;
    auto statement = make_stmt(Stmt::K::Return, line);
    if (!at(";")) {
      statement->expr = parse_expr();
      if (statement->expr == nullptr) return nullptr;
    }
    if (!expect(";")) return nullptr;
    return statement;
  }

  if (at("var") || at("let") || at("const")) {
    auto statement = make_stmt(Stmt::K::Var, line);
    statement->is_const = !at("var");
    position_++;
    if (accept("<")) {
      std::string space;
      if (!expect_ident(&space)) return nullptr;
      if (accept(",")) {
        std::string access;
        if (!expect_ident(&access)) return nullptr;
        statement->access = access;
      }
      if (!expect(">")) return nullptr;
    }
    if (!expect_ident(&statement->name)) return nullptr;
    if (accept(":")) {
      statement->type = parse_type();
      if (statement->type == nullptr) return nullptr;
    }
    if (accept("=")) {
      statement->expr = parse_expr();
      if (statement->expr == nullptr) return nullptr;
    }
    if (statement->type == nullptr && statement->expr == nullptr) {
      fail("a declaration needs a type or an initializer");
      return nullptr;
    }
    if (!expect(";")) return nullptr;
    return statement;
  }

  StmtPtr statement = parse_simple_stmt();
  if (statement == nullptr) return nullptr;
  if ((at("++") || at("--")) && statement->kind == Stmt::K::Expr) {
    auto increment = make_stmt(Stmt::K::Assign, line);
    increment->op = peek().text;
    position_++;
    increment->expr = std::move(statement->expr);
    if (!expect(";")) return nullptr;
    return increment;
  }
  if (!expect(";")) return nullptr;
  return statement;
}

}  // namespace wgsl
}  // namespace xtgui
