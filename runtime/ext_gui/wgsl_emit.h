/*
 * xbintsc GUI engine — WGSL -> GLSL emitter (shared declarations).
 *
 * Emission runs in three passes:
 *   1. mark the globals and functions reachable from the requested entry point,
 *      so a module that holds several stages only declares what this stage uses;
 *   2. assign SDL_GPU binding slots to the reachable resources and pair every
 *      sampled texture with the sampler(s) it is sampled with;
 *   3. emit structs, resource declarations, functions and the entry wrapper.
 */
#ifndef XT_GUI_WGSL_EMIT_H
#define XT_GUI_WGSL_EMIT_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include "wgsl.h"
#include "wgsl_ast.h"

namespace xtgui {
namespace wgsl {

class Emitter {
 public:
  Emitter(const Module &module, Stage stage, StageCode *out)
      : module_(module), stage_(stage), out_(out) {}

  bool run(const std::string &entry_point, CompileInfo *info);

  /* -- expression/statement emission (wgsl_emit_expr.cpp, wgsl_emit_stmt.cpp) */
  std::string expr(const Expr &node);
  std::string call_expr(const Expr &node);
  std::string statement(const Stmt &node, int indent);
  TypePtr type_of(const Expr &node);

  /* -- helpers shared by the emission units --------------------------------- */
  struct Local {
    std::string name;
    TypePtr type;
  };
  void push_scope();
  void pop_scope();
  void declare(const std::string &name, const TypePtr &type);
  const Local *lookup_local(const std::string &name) const;
  const GlobalDecl *find_global(const std::string &name) const;
  const FuncDecl *find_function(const std::string &name) const;
  const StructDecl *struct_of(const TypePtr &type) const;
  /** `xg_`-prefixed member name; swizzle letters pass through untouched. */
  std::string member_name(const std::string &name) const;
  /** The GLSL expression that names a global (`xg_u`, `xg_u.xg_data`, …). */
  std::string global_expr(const std::string &name) const;
  /** The sampler slot a texture uses together with `sampler` ("" = any). */
  int pair_slot_for(const std::string &texture, const std::string &sampler) const;
  int first_pair_for(const std::string &texture) const;
  /** GLSL builtin variable for a WGSL `@builtin(name)` ("" when unknown). */
  std::string builtin_variable(const std::string &name, Stage stage) const;
  /** Emit `target(expr)` only when the GLSL type would otherwise be wrong. */
  std::string cast_to(const Type &target, const std::string &value) const;
  std::string emit_user_function(const FuncDecl &func);
  bool emit_entry(const FuncDecl &entry, std::string *text);

  const Module &module_;
  Stage stage_;
  StageCode *out_;
  std::string structs_text_;
  std::string globals_text_;
  std::string functions_text_;
  std::string entry_text_;

  void fail(const std::string &message) {
    if (!failed_) failed_ = true;
    error_ = message;
  }
  bool failed_ = false;
  std::string error_;

 private:
  struct Resource {
    enum class Kind {
      Uniform,
      StorageRead,
      StorageWrite,
      StorageTextureRead,
      StorageTextureWrite,
      Texture,
      Sampler
    } kind = Kind::Uniform;
    int slot = -1;
    int binding = -1;
  };
  struct Pair {
    std::string texture;
    std::string sampler;  // empty for a texture used only by textureLoad
    int slot = -1;
  };

  bool collect_reachable(const FuncDecl &entry);
  void collect_function(const FuncDecl *func);
  void collect_stmt(const Stmt &node, bool &loads, bool &samples);
  void collect_expr(const Expr &node, bool &loads, bool &samples);
  bool assign_resources(CompileInfo *info);
  bool emit_declarations(CompileInfo *info);
  std::string block_body(const Type &type, bool storage) const;
  std::string storage_declaration(const GlobalDecl &decl, int set, int binding,
                                  const std::string &api_name) const;

  std::map<std::string, const FuncDecl *> functions_;
  std::map<std::string, const GlobalDecl *> globals_;
  std::map<std::string, Resource> resources_;
  std::vector<Pair> pairs_;
  std::map<std::string, std::string> global_expr_;
  std::vector<std::map<std::string, Local>> scopes_;
  std::vector<std::string> continuing_stack_;
  std::set<std::string> reachable_;
  std::set<std::string> visited_functions_;
  std::set<std::string> load_only_textures_;
  int local_counter_ = 0;
  int read_set_ = 0;
  int write_set_ = 1;
  int uniform_set_ = 2;
};

}  // namespace wgsl
}  // namespace xtgui

#endif /* XT_GUI_WGSL_EMIT_H */
