/*
 * xbintsc GUI engine — WGSL module setup, resource assignment and declarations.
 *
 * Reachability matters: a module usually holds both the vertex and fragment
 * entry point, and only the resources the requested stage actually touches may
 * become SDL_GPU bindings, or a fragment-only texture would force the vertex
 * stage to bind a texture the program never supplied.
 */

#include "wgsl_emit.h"

#include <algorithm>
#include <cstdlib>

namespace xtgui {
namespace wgsl {
namespace {

bool is_swizzle(const std::string &name) {
  if (name.empty() || name.size() > 4) return false;
  for (char c : name) {
    if (std::string("xyzwrgba").find(c) == std::string::npos) return false;
  }
  return true;
}

bool is_texture(const TypePtr &type) {
  return type != nullptr && (type->kind == Kind::Texture || type->kind == Kind::StorageTexture);
}

bool is_sampler(const TypePtr &type) {
  return type != nullptr && (type->kind == Kind::Sampler || type->kind == Kind::SamplerComparison);
}

bool texture_used_as_store(const GlobalDecl &decl) {
  return decl.type != nullptr && decl.type->kind == Kind::StorageTexture && decl.type->access != 1;
}

bool storage_written(const GlobalDecl &decl) {
  if (decl.access == "write" || decl.access == "read_write") return true;
  return decl.type != nullptr && decl.type->access != 1;
}

}  // namespace

/* -- shared helpers ------------------------------------------------------- */

void Emitter::push_scope() { scopes_.emplace_back(); }

void Emitter::pop_scope() {
  if (!scopes_.empty()) scopes_.pop_back();
}

void Emitter::declare(const std::string &name, const TypePtr &type) {
  if (scopes_.empty()) push_scope();
  Local local;
  local.name = "xg_" + name + "_" + std::to_string(local_counter_++);
  local.type = type;
  scopes_.back()[name] = local;
}

const Emitter::Local *Emitter::lookup_local(const std::string &name) const {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    auto found = scope->find(name);
    if (found != scope->end()) return &found->second;
  }
  return nullptr;
}

const GlobalDecl *Emitter::find_global(const std::string &name) const {
  auto found = globals_.find(name);
  return found == globals_.end() ? nullptr : found->second;
}

const FuncDecl *Emitter::find_function(const std::string &name) const {
  auto found = functions_.find(name);
  return found == functions_.end() ? nullptr : found->second;
}

const StructDecl *Emitter::struct_of(const TypePtr &type) const {
  return type != nullptr && type->kind == Kind::Struct ? type->strct : nullptr;
}

std::string Emitter::member_name(const std::string &name) const {
  if (is_swizzle(name)) return name;
  return "xg_" + name;
}

std::string Emitter::global_expr(const std::string &name) const {
  auto found = global_expr_.find(name);
  return found == global_expr_.end() ? "xg_" + name : found->second;
}

int Emitter::pair_slot_for(const std::string &texture, const std::string &sampler) const {
  for (const Pair &pair : pairs_) {
    if (pair.texture == texture && (sampler.empty() || pair.sampler == sampler)) return pair.slot;
  }
  return -1;
}

int Emitter::first_pair_for(const std::string &texture) const {
  return pair_slot_for(texture, std::string());
}

/* -- reachability --------------------------------------------------------- */

void Emitter::collect_expr(const Expr &node, bool &loads, bool &samples) {
  if (node.kind == Expr::K::Ident) {
    if (globals_.count(node.text) != 0) reachable_.insert(node.text);
    return;
  }
  if (node.kind == Expr::K::Call) {
    const std::string &callee = node.text;
    if (callee.compare(0, 5, ".") != 0 && callee.compare(0, 13, "textureSample") == 0) {
      samples = true;
      if (node.args.size() >= 2 && node.args[0]->kind == Expr::K::Ident &&
          node.args[1]->kind == Expr::K::Ident) {
        const std::string &texture = node.args[0]->text;
        const std::string &sampler = node.args[1]->text;
        bool exists = false;
        for (const Pair &pair : pairs_) {
          if (pair.texture == texture && pair.sampler == sampler) exists = true;
        }
        if (!exists) pairs_.push_back({texture, sampler, -1});
        reachable_.insert(texture);
        reachable_.insert(sampler);
      }
    } else if (callee == "textureLoad" || callee == "textureDimensions" ||
               callee == "textureNumLevels" || callee == "textureNumLayers") {
      loads = true;
      if (!node.args.empty() && node.args[0]->kind == Expr::K::Ident) {
        load_only_textures_.insert(node.args[0]->text);
        reachable_.insert(node.args[0]->text);
      }
    } else if (callee == "textureStore") {
      if (!node.args.empty() && node.args[0]->kind == Expr::K::Ident) {
        reachable_.insert(node.args[0]->text);
      }
    } else if (const FuncDecl *function = find_function(callee)) {
      if (reachable_.count("fn:" + callee) == 0) collect_function(function);
    }
    for (const ExprPtr &arg : node.args) {
      if (arg != nullptr) collect_expr(*arg, loads, samples);
    }
    return;
  }
  for (const ExprPtr &arg : node.args) {
    if (arg != nullptr) collect_expr(*arg, loads, samples);
  }
}

void Emitter::collect_stmt(const Stmt &node, bool &loads, bool &samples) {
  if (node.expr != nullptr) collect_expr(*node.expr, loads, samples);
  if (node.value != nullptr) collect_expr(*node.value, loads, samples);
  for (const StmtPtr &child : node.body) {
    if (child != nullptr) collect_stmt(*child, loads, samples);
  }
  for (const StmtPtr &child : node.otherwise) {
    if (child != nullptr) collect_stmt(*child, loads, samples);
  }
  if (node.init != nullptr) collect_stmt(*node.init, loads, samples);
  if (node.cond != nullptr) collect_stmt(*node.cond, loads, samples);
  if (node.update != nullptr) collect_stmt(*node.update, loads, samples);
  if (node.continuing != nullptr) collect_stmt(*node.continuing, loads, samples);
  for (const StmtPtr &child : node.case_bodies) {
    if (child != nullptr) collect_stmt(*child, loads, samples);
  }
}

void Emitter::collect_function(const FuncDecl *func) {
  if (func == nullptr || visited_functions_.count(func->name) != 0) return;
  visited_functions_.insert(func->name);
  reachable_.insert("fn:" + func->name);
  bool loads = false;
  bool samples = false;
  for (const StmtPtr &statement : func->body) {
    if (statement != nullptr) collect_stmt(*statement, loads, samples);
  }
}

bool Emitter::collect_reachable(const FuncDecl &entry) {
  collect_function(&entry);
  return true;
}

/* -- slot assignment ------------------------------------------------------ */

bool Emitter::assign_resources(CompileInfo *info) {
  const bool compute = stage_ == Stage::Compute;
  read_set_ = compute ? 0 : (stage_ == Stage::Fragment ? 2 : 0);
  write_set_ = 1;
  uniform_set_ = compute ? 2 : (stage_ == Stage::Fragment ? 3 : 1);

  auto by_binding = [&](const GlobalDecl *left, const GlobalDecl *right) {
    if (left->group != right->group) return left->group < right->group;
    return left->binding < right->binding;
  };
  std::vector<const GlobalDecl *> textures;
  std::vector<const GlobalDecl *> storage_buffers;
  std::vector<const GlobalDecl *> storage_textures;
  std::vector<const GlobalDecl *> uniforms;
  for (const auto &entry_global : globals_) {
    const GlobalDecl *decl = entry_global.second;
    if (reachable_.count(decl->name) == 0) continue;
    if (is_texture(decl->type) && decl->type->kind == Kind::Texture) textures.push_back(decl);
    else if (decl->space == Space::Storage) {
      if (decl->type != nullptr && decl->type->kind == Kind::Struct) storage_buffers.push_back(decl);
      else if (decl->type != nullptr && decl->type->kind == Kind::Array) storage_buffers.push_back(decl);
      else if (decl->type != nullptr && decl->type->kind == Kind::StorageTexture) {
        storage_textures.push_back(decl);
      } else storage_buffers.push_back(decl);
    } else if (decl->space == Space::Uniform) uniforms.push_back(decl);
  }
  std::sort(textures.begin(), textures.end(), by_binding);
  std::sort(storage_buffers.begin(), storage_buffers.end(), by_binding);
  std::sort(storage_textures.begin(), storage_textures.end(), by_binding);
  std::sort(uniforms.begin(), uniforms.end(), by_binding);

  /* Sampler slots: one per (texture, sampler) pair. Ensure every reachable
   * texture has at least one slot so textureLoad-only textures can be bound. */
  for (const GlobalDecl *decl : textures) {
    bool any = false;
    for (const Pair &pair : pairs_) {
      if (pair.texture == decl->name) any = true;
    }
    if (!any) pairs_.push_back({decl->name, std::string(), -1});
  }
  std::sort(pairs_.begin(), pairs_.end(), [&](const Pair &left, const Pair &right) {
    const GlobalDecl *a = find_global(left.texture);
    const GlobalDecl *b = find_global(right.texture);
    if (a != nullptr && b != nullptr) {
      if (a->group != b->group) return a->group < b->group;
      if (a->binding != b->binding) return a->binding < b->binding;
    }
    const GlobalDecl *as = left.sampler.empty() ? nullptr : find_global(left.sampler);
    const GlobalDecl *bs = right.sampler.empty() ? nullptr : find_global(right.sampler);
    if (as == nullptr && bs == nullptr) return left.sampler < right.sampler;
    if (as == nullptr) return true;
    if (bs == nullptr) return false;
    if (as->group != bs->group) return as->group < bs->group;
    return as->binding < bs->binding;
  });
  if (pairs_.size() > 16) {
    info->message = "at most 16 sampled textures can be used by one stage";
    return false;
  }
  for (size_t i = 0; i < pairs_.size(); i++) {
    pairs_[i].slot = (int)i;
    TextureSlot slot;
    const GlobalDecl *texture = find_global(pairs_[i].texture);
    if (texture == nullptr) continue;
    slot.texture.group = texture->group;
    slot.texture.binding = texture->binding;
    if (!pairs_[i].sampler.empty()) {
      const GlobalDecl *sampler = find_global(pairs_[i].sampler);
      if (sampler != nullptr) {
        slot.has_sampler = true;
        slot.sampler.group = sampler->group;
        slot.sampler.binding = sampler->binding;
      }
    }
    out_->samplers.push_back(slot);
    Resource resource;
    resource.kind = Resource::Kind::Texture;
    resource.slot = (int)i;
    resources_[pairs_[i].texture] = resource;
    if (!pairs_[i].sampler.empty()) {
      Resource sampler_resource;
      sampler_resource.kind = Resource::Kind::Sampler;
      sampler_resource.slot = (int)i;
      resources_[pairs_[i].sampler] = sampler_resource;
    }
  }

  const int pair_count = (int)pairs_.size();
  for (size_t i = 0; i < storage_textures.size(); i++) {
    const GlobalDecl *decl = storage_textures[i];
    Resource resource;
    if (texture_used_as_store(*decl)) {
      resource.kind = Resource::Kind::StorageTextureWrite;
      resource.slot = (int)i;
      out_->write_storage_textures.push_back({decl->group, decl->binding});
    } else {
      resource.kind = Resource::Kind::StorageTextureRead;
      resource.slot = (int)i;
      out_->storage_textures.push_back({decl->group, decl->binding});
    }
    resources_[decl->name] = resource;
  }
  int read_storage_buffers = 0;
  int write_storage_buffers = 0;
  int read_binding = pair_count + (int)out_->storage_textures.size();
  for (const GlobalDecl *decl : storage_buffers) {
    Resource resource;
    if (storage_written(*decl)) {
      resource.kind = Resource::Kind::StorageWrite;
      resource.slot = write_storage_buffers++;
      out_->write_storage_buffers.push_back({decl->group, decl->binding});
    } else {
      resource.kind = Resource::Kind::StorageRead;
      resource.slot = read_storage_buffers++;
      resource.binding = read_binding + read_storage_buffers - 1;
      out_->storage_buffers.push_back({decl->group, decl->binding});
    }
    resources_[decl->name] = resource;
  }
  if (uniforms.size() > 4) {
    info->message = "at most 4 uniform buffers can be used by one stage";
    return false;
  }
  for (size_t i = 0; i < uniforms.size(); i++) {
    Resource resource;
    resource.kind = Resource::Kind::Uniform;
    resource.slot = (int)i;
    resources_[uniforms[i]->name] = resource;
    out_->uniform_buffers.push_back({uniforms[i]->group, uniforms[i]->binding});
  }
  return true;
}

/* -- declarations --------------------------------------------------------- */

std::string Emitter::block_body(const Type &type, bool storage) const {
  std::string body;
  if (type.kind == Kind::Struct && type.strct != nullptr) {
    for (const Member &member : type.strct->members) {
      if (member.type == nullptr) continue;
      body += "  " + glsl_decl(*member.type, member_name(member.name)) + ";\n";
    }
    return body;
  }
  if (type.kind == Kind::Array && type.elem != nullptr) {
    if (storage && type.length == 0) {
      body += "  " + glsl_type(*type.elem) + " xg_data[];\n";
    } else {
      body += "  " + glsl_decl(type, "xg_data") + ";\n";
    }
    return body;
  }
  body += "  " + glsl_decl(type, "xg_value") + ";\n";
  return body;
}

bool Emitter::emit_declarations(CompileInfo *info) {
  /* Struct definitions first: blocks and function signatures use them. */
  for (const StructDecl &decl : module_.structs) {
    structs_text_ += "struct " + decl.name + " {\n";
    for (const Member &member : decl.members) {
      if (member.type == nullptr) continue;
      structs_text_ += "  " + glsl_decl(*member.type, member_name(member.name)) + ";\n";
    }
    structs_text_ += "};\n";
  }

  for (const auto &entry : globals_) {
    const GlobalDecl *decl = entry.second;
    const std::string name = "xg_" + decl->name;
    if (decl->type == nullptr) {
      if (decl->init == nullptr) continue;
      /* `const positions = array<vec2f, 3>(…);` has no written type. */
      TypePtr inferred = type_of(*decl->init);
      if (inferred == nullptr || inferred->kind == Kind::Unknown) continue;
      const_cast<GlobalDecl *>(decl)->type = inferred;
    }
    const bool used = reachable_.count(decl->name) != 0;

    /* Textures and samplers are resources regardless of the address space the
     * parser defaulted them to (a module-scope `var tex: texture_2d<f32>;`
     * omits `var<…>` entirely). */
    if (is_sampler(decl->type)) continue;
    if (decl->type->kind == Kind::Texture) {
      if (!used) continue;
      const int slot = first_pair_for(decl->name);
      if (slot < 0) continue;
      const bool depth = decl->type->depth;
      globals_text_ += "layout(set = " + std::to_string(read_set_) + ", binding = " +
                       std::to_string(slot) + ") uniform " +
                       (depth ? "sampler2DShadow" : "sampler2D") + " xg_tex" +
                       std::to_string(slot) + ";\n";
      global_expr_[decl->name] = "xg_tex" + std::to_string(slot);
      continue;
    }
    if (decl->type->kind == Kind::StorageTexture) {
      if (!used) continue;
      Resource resource = resources_[decl->name];
      const bool write = resource.kind == Resource::Kind::StorageTextureWrite;
      const int set = write ? write_set_ : read_set_;
      const int binding = write ? resource.slot : (int)pairs_.size() + resource.slot;
      std::string image = "image2D";
      switch (decl->type->dim) {
        case TextureDim::D2Array: image = "image2DArray"; break;
        case TextureDim::D3: image = "image3D"; break;
        default: break;
      }
      globals_text_ += "layout(" + image_format_glsl(decl->type->format) + ", set = " +
                       std::to_string(set) + ", binding = " + std::to_string(binding) +
                       ") uniform " + (write ? "writeonly " : "readonly ") + image + " " + name +
                       ";\n";
      global_expr_[decl->name] = name;
      continue;
    }

    if (decl->space == Space::None) {
      globals_text_ += std::string("const ") + glsl_decl(*decl->type, name);
      if (decl->init != nullptr) globals_text_ += " = " + expr(*decl->init);
      globals_text_ += ";\n";
      global_expr_[decl->name] = name;
      continue;
    }
    if (decl->space == Space::Private) {
      globals_text_ += glsl_decl(*decl->type, name);
      if (decl->init != nullptr) globals_text_ += " = " + expr(*decl->init);
      globals_text_ += ";\n";
      global_expr_[decl->name] = name;
      continue;
    }
    if (decl->space == Space::Workgroup) {
      globals_text_ += std::string("shared ") + glsl_decl(*decl->type, name) + ";\n";
      global_expr_[decl->name] = name;
      continue;
    }
    if (!used) continue;

    if (decl->space == Space::Uniform) {
      Resource resource = resources_[decl->name];
      globals_text_ += "layout(std140, set = " + std::to_string(uniform_set_) +
                       ", binding = " + std::to_string(resource.slot) + ") uniform " + name +
                       "_block {\n" + block_body(*decl->type, false) + "} " + name + ";\n";
      if (decl->type->kind == Kind::Struct) global_expr_[decl->name] = name;
      else if (decl->type->kind == Kind::Array) global_expr_[decl->name] = name + ".xg_data";
      else global_expr_[decl->name] = name + ".xg_value";
      continue;
    }
    if (decl->space == Space::Storage) {
      Resource resource = resources_[decl->name];
      const bool write = resource.kind == Resource::Kind::StorageWrite;
      const int set = write ? write_set_ : read_set_;
      const int binding = write ? resource.slot : resource.binding;
      globals_text_ += "layout(std430, set = " + std::to_string(set) +
                       ", binding = " + std::to_string(binding) + ") buffer " + name +
                       "_block {\n" + block_body(*decl->type, true) + "} " + name + ";\n";
      if (decl->type->kind == Kind::Struct) global_expr_[decl->name] = name;
      else if (decl->type->kind == Kind::Array) global_expr_[decl->name] = name + ".xg_data";
      else global_expr_[decl->name] = name + ".xg_value";
    }
  }
  (void)info;
  return true;
}

}  // namespace wgsl
}  // namespace xtgui
