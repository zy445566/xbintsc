/*
 * xbintsc GUI engine — WGSL expressions.
 *
 * A precedence-climbing parser. Calls are recorded as `Call` nodes; a method
 * call such as `array.length()` is stored with the synthetic callee
 * `.length` and the receiver as the first argument, which keeps the emitter's
 * name resolution in one place.
 */

#include "wgsl_parser.h"

#include <cstdlib>

namespace xtgui {
namespace wgsl {
namespace {

int precedence(const std::string &op) {
  if (op == "||") return 1;
  if (op == "&&") return 2;
  if (op == "|") return 3;
  if (op == "^") return 4;
  if (op == "&") return 5;
  if (op == "==" || op == "!=") return 6;
  if (op == "<" || op == ">" || op == "<=" || op == ">=") return 7;
  if (op == "<<" || op == ">>") return 8;
  if (op == "+" || op == "-") return 9;
  if (op == "*" || op == "/" || op == "%") return 10;
  return 0;
}

bool is_binary(const Token &token) {
  return token.kind == Tok::Punct && precedence(token.text) > 0;
}

}  // namespace

ExprPtr Parser::parse_expr(int min_precedence) {
  ExprPtr left = parse_unary();
  if (left == nullptr) return nullptr;
  while (!failed_ && is_binary(peek())) {
    const std::string op = peek().text;
    const int prec = precedence(op);
    if (prec < min_precedence) break;
    const int line = peek().line;
    position_++;
    ExprPtr right = parse_expr(prec + 1);
    if (right == nullptr) return nullptr;
    auto node = std::make_unique<Expr>();
    node->kind = Expr::K::Binary;
    node->text = op;
    node->line = line;
    node->args.push_back(std::move(left));
    node->args.push_back(std::move(right));
    left = std::move(node);
  }
  return left;
}

ExprPtr Parser::parse_unary() {
  if (peek().kind == Tok::Punct &&
      (at("-") || at("!") || at("~") || at("&") || at("*"))) {
    auto node = std::make_unique<Expr>();
    node->kind = Expr::K::Unary;
    node->text = peek().text;
    node->line = peek().line;
    position_++;
    ExprPtr operand = parse_unary();
    if (operand == nullptr) return nullptr;
    node->args.push_back(std::move(operand));
    return node;
  }
  return parse_postfix();
}

ExprPtr Parser::parse_postfix() {
  ExprPtr value = parse_primary();
  if (value == nullptr) return nullptr;
  while (!failed_) {
    if (accept("[")) {
      ExprPtr index = parse_expr();
      if (index == nullptr) return nullptr;
      if (!expect("]")) return nullptr;
      auto node = std::make_unique<Expr>();
      node->kind = Expr::K::Index;
      node->line = peek().line;
      node->args.push_back(std::move(value));
      node->args.push_back(std::move(index));
      value = std::move(node);
      continue;
    }
    if (accept(".")) {
      std::string member;
      if (!expect_ident(&member)) return nullptr;
      auto node = std::make_unique<Expr>();
      node->kind = Expr::K::Member;
      node->member = member;
      node->line = peek().line;
      node->args.push_back(std::move(value));
      value = std::move(node);
      continue;
    }
    if (accept("(")) {
      std::vector<ExprPtr> args;
      while (!at(")") && !failed_) {
        ExprPtr arg = parse_expr();
        if (arg == nullptr) return nullptr;
        args.push_back(std::move(arg));
        if (!accept(",")) break;
      }
      if (!expect(")")) return nullptr;
      auto node = std::make_unique<Expr>();
      node->kind = Expr::K::Call;
      node->line = peek().line;
      if (value->kind == Expr::K::Member) {
        node->text = "." + value->member;
        node->args.push_back(std::move(value->args[0]));
      } else if (value->kind == Expr::K::Ident) {
        node->text = value->text;
      } else {
        fail("only named functions and methods can be called");
        return nullptr;
      }
      for (ExprPtr &arg : args) node->args.push_back(std::move(arg));
      value = std::move(node);
      continue;
    }
    break;
  }
  return value;
}

ExprPtr Parser::parse_primary() {
  const Token token = peek();
  if (token.kind == Tok::Int || token.kind == Tok::Float) {
    position_++;
    auto node = std::make_unique<Expr>();
    node->kind = token.kind == Tok::Int ? Expr::K::Int : Expr::K::Float;
    node->text = token.text;
    node->line = token.line;
    return node;
  }
  if (token.kind == Tok::Ident && (token.text == "true" || token.text == "false")) {
    position_++;
    auto node = std::make_unique<Expr>();
    node->kind = Expr::K::Bool;
    node->text = token.text;
    node->line = token.line;
    return node;
  }
  if (token.kind == Tok::Ident && token.text == "array" && peek(1).text == "<") {
    position_ += 2;
    TypePtr elem = parse_type();
    if (elem == nullptr) return nullptr;
    int length = 0;
    if (accept(",")) {
      if (peek().kind != Tok::Int) {
        fail("an array length must be an integer literal");
        return nullptr;
      }
      length = std::atoi(peek().text.c_str());
      position_++;
    }
    if (!expect(">")) return nullptr;
    auto node = std::make_unique<Expr>();
    node->kind = Expr::K::ArrayCtor;
    node->type = make_array(elem, length);
    node->line = token.line;
    if (accept("(")) {
      while (!at(")") && !failed_) {
        ExprPtr arg = parse_expr();
        if (arg == nullptr) return nullptr;
        node->args.push_back(std::move(arg));
        if (!accept(",")) break;
      }
      if (!expect(")")) return nullptr;
    }
    return node;
  }
  if (token.kind == Tok::Ident && token.text == "bitcast" && peek(1).text == "<") {
    position_ += 2;
    TypePtr target = parse_type();
    if (target == nullptr) return nullptr;
    if (!expect(">")) return nullptr;
    if (!expect("(")) return nullptr;
    ExprPtr arg = parse_expr();
    if (arg == nullptr) return nullptr;
    if (!expect(")")) return nullptr;
    auto node = std::make_unique<Expr>();
    node->kind = Expr::K::Bitcast;
    node->type = target;
    node->line = token.line;
    node->args.push_back(std::move(arg));
    return node;
  }
  if (token.kind == Tok::Ident) {
    position_++;
    auto node = std::make_unique<Expr>();
    node->kind = Expr::K::Ident;
    node->text = token.text;
    node->line = token.line;
    return node;
  }
  if (accept("(")) {
    ExprPtr inner = parse_expr();
    if (inner == nullptr) return nullptr;
    if (!expect(")")) return nullptr;
    return inner;
  }
  fail("expected an expression");
  return nullptr;
}

}  // namespace wgsl
}  // namespace xtgui
