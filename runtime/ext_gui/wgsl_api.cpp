/*
 * xbintsc GUI engine — WGSL translation entry points.
 */

#include "wgsl.h"

#include <cstdlib>
#include <string>

#include "wgsl_ast.h"
#include "wgsl_emit.h"
#include "wgsl_lexer.h"
#include "wgsl_parser.h"

namespace xtgui {
namespace wgsl {

namespace {

bool parse_source(const std::string &source, Module *module, CompileInfo *info) {
  std::vector<Token> tokens;
  std::string error;
  if (!tokenize(source, &tokens, &error)) {
    info->message = error;
    const size_t colon = error.find(':');
    if (colon != std::string::npos) {
      info->line = std::atoi(error.c_str());
      const size_t second = error.find(':', colon + 1);
      if (second != std::string::npos) info->column = std::atoi(error.c_str() + colon + 1);
    }
    return false;
  }
  Parser parser(tokens, module);
  if (!parser.parse_module()) {
    info->message = parser.error();
    info->line = parser.error_line();
    info->column = parser.error_column();
    return false;
  }
  return true;
}

std::string stage_name(Stage stage) {
  switch (stage) {
    case Stage::Vertex: return "vertex";
    case Stage::Fragment: return "fragment";
    default: return "compute";
  }
}

}  // namespace

bool Emitter::run(const std::string &entry_point, CompileInfo *info) {
  for (const FuncDecl &function : module_.funcs) functions_[function.name] = &function;
  for (const GlobalDecl &global : module_.globals) globals_[global.name] = &global;

  const FuncDecl *entry = find_function(entry_point);
  if (entry == nullptr) {
    info->message = "entry point '" + entry_point + "' was not declared in this shader module";
    return false;
  }
  if (stage_name(stage_) != entry->stage) {
    info->message = "entry point '" + entry_point + "' is a @" + entry->stage +
                    " entry point, not @" + stage_name(stage_);
    return false;
  }
  if (!entry->is_entry) {
    info->message = "the function '" + entry_point + "' is not an entry point";
    return false;
  }

  collect_reachable(*entry);
  if (!assign_resources(info)) return false;
  if (!emit_declarations(info)) return false;

  /* Prototypes first: WGSL allows a function to call one declared later. */
  std::vector<const FuncDecl *> reachable;
  for (const std::string &name : visited_functions_) {
    if (name == entry_point) continue;
    const FuncDecl *function = find_function(name);
    if (function != nullptr) reachable.push_back(function);
  }
  for (const FuncDecl *function : reachable) {
    Type void_type;
    void_type.kind = Kind::Void;
    functions_text_ += glsl_type(function->ret != nullptr ? *function->ret : void_type) + " xg_" +
                       function->name + "(";
    for (size_t i = 0; i < function->params.size(); i++) {
      if (i > 0) functions_text_ += ", ";
      functions_text_ += glsl_decl(*function->params[i].type,
                                   "xg_" + function->params[i].name + "_p");
    }
    functions_text_ += ");\n";
  }
  for (const FuncDecl *function : reachable) {
    functions_text_ += emit_user_function(*function);
  }

  if (!emit_entry(*entry, &entry_text_)) return false;
  if (failed_) {
    info->message = error_;
    return false;
  }

  out_->glsl = "#version 450\n";
  out_->glsl += structs_text_;
  out_->glsl += globals_text_;
  out_->glsl += functions_text_;
  out_->glsl += entry_text_;
  out_->entry = "main";
  return true;
}

bool translate(const std::string &source, const std::string &entry_point, Stage stage,
               StageCode *out, CompileInfo *info) {
  if (out == nullptr || info == nullptr) return false;
  *out = StageCode();
  info->message.clear();
  info->line = 0;
  info->column = 0;
  Module module;
  if (!parse_source(source, &module, info)) return false;
  Emitter emitter(module, stage, out);
  if (!emitter.run(entry_point, info)) return false;
  return true;
}

bool check(const std::string &source, CompileInfo *info) {
  info->message.clear();
  info->line = 0;
  info->column = 0;
  Module module;
  return parse_source(source, &module, info);
}

}  // namespace wgsl
}  // namespace xtgui
