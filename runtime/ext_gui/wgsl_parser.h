/*
 * xbintsc GUI engine — recursive-descent WGSL parser (shared declarations).
 */
#ifndef XT_GUI_WGSL_PARSER_H
#define XT_GUI_WGSL_PARSER_H

#include <string>
#include <vector>

#include "wgsl_ast.h"
#include "wgsl_lexer.h"

namespace xtgui {
namespace wgsl {

class Parser {
 public:
  Parser(const std::vector<Token> &tokens, Module *module)
      : tokens_(tokens), module_(module) {}

  bool parse_module();

  /* -- token helpers ------------------------------------------------------ */
  const Token &peek(int ahead = 0) const;
  bool at(const std::string &text) const;
  bool accept(const std::string &text);
  bool expect(const std::string &text);
  bool expect_ident(std::string *out);
  void fail(const std::string &message);
  void fail_at(const Token &token, const std::string &message);
  bool ok() const { return !failed_; }

  /* -- declarations ------------------------------------------------------- */
  bool parse_struct();
  bool parse_global(const std::vector<Attribute> &attrs);
  bool parse_const_global();
  bool parse_alias();
  bool parse_function(const std::vector<Attribute> &attrs);

  /* -- types and attributes ----------------------------------------------- */
  bool parse_attrs(std::vector<Attribute> *attrs);
  TypePtr parse_type();
  TypePtr parse_texture_type(const std::string &name);
  bool expect_close_angle();
  bool parse_workgroup_size(const Attribute &attr, int out[3]);

  /* -- statements and expressions ----------------------------------------- */
  StmtPtr parse_stmt();
  StmtPtr parse_block();
  StmtPtr parse_simple_stmt();
  ExprPtr parse_expr(int min_precedence = 0);
  ExprPtr parse_unary();
  ExprPtr parse_postfix();
  ExprPtr parse_primary();

  const std::string &error() const { return error_; }
  int error_line() const { return error_line_; }
  int error_column() const { return error_column_; }

 private:
  const Token &token(size_t index) const;
  bool consume_angle();

  const std::vector<Token> &tokens_;
  Module *module_ = nullptr;
  size_t position_ = 0;
  int pending_angles_ = 0;
  bool failed_ = false;
  std::string error_;
  int error_line_ = 0;
  int error_column_ = 0;
};

}  // namespace wgsl
}  // namespace xtgui

#endif /* XT_GUI_WGSL_PARSER_H */
